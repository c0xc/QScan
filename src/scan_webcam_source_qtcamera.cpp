/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (p@c0xc.net)
** This file is part of QScan.
**
** QScan is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** QScan is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with QScan. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

//Qt6 QCamera implementation for webcam capture
#ifdef USE_QTCAMERA

#include "scan/webcam_backend.hpp"
#include "scan/scan_device_info.hpp"
#include "core/classlogger.hpp"
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaDevices>
#include <QCameraDevice>
#include <memory>

class QtCameraWebcamBackend : public WebcamBackend
{
public:

    QtCameraWebcamBackend()
    {
    }

    ~QtCameraWebcamBackend() override
    {
        //Stop stream before QObject teardown
        stopPreview();
    }

    bool
    initialize(const QString &device_id) override
    {
        QList<QCameraDevice> camera_devices = QMediaDevices::videoInputs();
        QCameraDevice selected_device;

        for (const QCameraDevice &cam_device : camera_devices)
        {
            if (QString::fromUtf8(cam_device.id()) == device_id)
            {
                selected_device = cam_device;
                break;
            }
        }

        if (selected_device.isNull())
        {
            Debug(QS("FAILED to find camera device with id <%s>", CSTR(device_id)));
            return false;
        }

        //Backend state,owned here (no ScanSource private access)
        m_camera.reset(new QCamera(selected_device));
        m_video_sink.reset(new QVideoSink());
        m_capture_session.reset(new QMediaCaptureSession());
        m_capture_session->setCamera(m_camera.get());
        m_capture_session->setVideoSink(m_video_sink.get());

        Debug(QS("Successfully created Qt6 QCamera for <%s>", CSTR(device_id)));
        return true;
    }

    QImage
    captureFrame() override
    {
        if (!m_video_sink)
            return QImage();

        QVideoFrame frame = m_video_sink->videoFrame();
        if (!frame.isValid())
        {
            Debug(QS("No valid video frame available"));
            return QImage();
        }

        QImage image = frame.toImage();
        if (image.isNull())
        {
            Debug(QS("Failed to convert video frame to image"));
            return QImage();
        }

        return image;
    }

    bool
    startPreview() override
    {
        if (m_camera)
        {
            m_camera->start();
            return true;
        }
        return false;
    }

    void
    stopPreview() override
    {
        if (m_camera)
            m_camera->stop();
    }

private:

    //Qt backend state,auto-cleaned by unique_ptr
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_capture_session;
    std::unique_ptr<QVideoSink> m_video_sink;

};

std::unique_ptr<WebcamBackend>
createWebcamBackend_QtCamera()
{
    return std::unique_ptr<WebcamBackend>(new QtCameraWebcamBackend());
}

QList<ScanDeviceInfo>
enumerateDevices_QtCamera()
{
    QList<ScanDeviceInfo> devices;
    
    Debug(QS("Enumerating Qt6 QCamera devices..."));

    //Get list of available cameras from Qt6
    QList<QCameraDevice> camera_devices = QMediaDevices::videoInputs();
    
    Debug(QS("Found %d camera device(s)", camera_devices.size()));

    for (int i = 0; i < camera_devices.size(); ++i)
    {
        const QCameraDevice &cam_device = camera_devices.at(i);
        QString identifier = QString::fromUtf8(cam_device.id());
        QString desc = cam_device.description();
        
        if (desc.isEmpty())
            desc = QString("Camera %1").arg(i);

        Debug(QS("Found Qt6 camera [%d]: id=<%s>, description=<%s>",
                 i, CSTR(identifier), CSTR(desc)));

        ScanDeviceInfo info(identifier, desc, ScanDeviceType::CAMERA);
        devices.append(info);
    }

    Debug(QS("Enumeration complete, found %d Qt6 webcam(s)", devices.size()));
    
    return devices;
}

#endif //USE_QTCAMERA
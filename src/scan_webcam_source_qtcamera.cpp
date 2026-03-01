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

#include <memory>

#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QImage>

#include "scan/webcam_backend.hpp"
#include "scan/scan_device_info.hpp"
#include "core/classlogger.hpp"

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
        QString raw_id = device_id;
        if (raw_id.startsWith(QStringLiteral("qcamera:")))
            raw_id = raw_id.mid(QStringLiteral("qcamera:").size());
        else if (raw_id.startsWith(QStringLiteral("qtcamera:")))
            raw_id = raw_id.mid(QStringLiteral("qtcamera:").size());

        QList<QCameraDevice> camera_devices = QMediaDevices::videoInputs();
        QCameraDevice selected_device;

        for (const QCameraDevice &cam_device : camera_devices)
        {
            if (QString::fromUtf8(cam_device.id()) == raw_id)
            {
                selected_device = cam_device;
                break;
            }
        }

        if (selected_device.isNull())
        {
            Debug(QS("FAILED to find camera device with id <%s> (raw=%s)", CSTR(device_id), CSTR(raw_id)));
            return false;
        }

        //Backend state,owned here (no ScanSource private access)
        m_camera.reset(new QCamera(selected_device));
        m_video_sink.reset(new QVideoSink());
        m_capture_session.reset(new QMediaCaptureSession());
        m_capture_session->setCamera(m_camera.get());
        m_capture_session->setVideoSink(m_video_sink.get());

        QObject::connect(
            m_video_sink.get(),
            &QVideoSink::videoFrameChanged,
            [this](const QVideoFrame &frame)
            {
                if (!frame.isValid())
                    return;
                const QImage img = frame.toImage();
                if (img.isNull())
                    return;
                m_last_image = img;
            }
        );

        QObject::connect(
            m_camera.get(),
            &QCamera::errorOccurred,
            [](QCamera::Error error, const QString &error_string)
            {
                Debug(QS("QtCamera errorOccurred: %d (%s)", static_cast<int>(error), CSTR(error_string)));
            }
        );

        Debug(QS("Successfully created Qt6 QCamera for <%s>", CSTR(device_id)));
        return true;
    }

    QImage
    captureFrame() override
    {
        if (m_last_image.isNull())
            Debug(QS("No valid video frame available"));
        return m_last_image;
    }

    bool
    startPreview() override
    {
        if (m_camera)
        {
            m_last_image = QImage();
            m_camera->start();
            Debug(QS("QtCamera start(): active=%d", m_camera->isActive() ? 1 : 0));
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
    QImage m_last_image;

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
    
    Debug(QS("Found %lld camera device(s)", static_cast<long long>(camera_devices.size())));

    for (int i = 0; i < camera_devices.size(); ++i)
    {
        const QCameraDevice &cam_device = camera_devices.at(i);
        const QString raw_id = QString::fromUtf8(cam_device.id());
        const QString identifier = QStringLiteral("qcamera:") + raw_id;
        QString desc = cam_device.description();

        const QString desc_norm = desc.trimmed().toLower();
        if (desc_norm.isEmpty() || desc_norm == QStringLiteral("v4l2"))
            desc = QString("Camera %1").arg(i + 1);

        Debug(QS("Found Qt6 camera [%d]: id=<%s>, description=<%s>",
                 i, CSTR(identifier), CSTR(desc)));

        ScanDeviceInfo info(identifier, desc, ScanDeviceType::CAMERA);
        devices.append(info);
    }

    Debug(QS("Enumeration complete, found %lld Qt6 webcam(s)", static_cast<long long>(devices.size())));
    
    return devices;
}

#endif //USE_QTCAMERA
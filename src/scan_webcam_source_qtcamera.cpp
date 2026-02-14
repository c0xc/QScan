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

#include "scan/webcamsource.hpp"
#include "core/classlogger.hpp"
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaDevices>
#include <QCameraDevice>

//Qt6 QCamera objects for webcam capture (managed by Qt parent-child system)
struct WebcamSource::PlatformData
{
    QCamera *camera;
    QMediaCaptureSession *capture_session;
    QVideoSink *video_sink;

    PlatformData()
        : camera(0), capture_session(0), video_sink(0)
    {}
};

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
        QString identifier = cam_device.id();
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

bool
initialize_QtCamera(WebcamSource *source, const QString &device_id)
{
    WebcamSource::PlatformData *data = source->m_platform_data.get();
    
    //Find camera device by identifier
    QList<QCameraDevice> camera_devices = QMediaDevices::videoInputs();
    QCameraDevice selected_device;
    
    for (const QCameraDevice &cam_device : camera_devices)
    {
        if (cam_device.id() == device_id)
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

    //Create Qt6 QCamera instance with WebcamSource as parent (Qt manages lifetime)
    data->camera = new QCamera(selected_device, source);
    if (!data->camera)
    {
        Debug(QS("FAILED to create QCamera instance"));
        return false;
    }

    //Create video sink and capture session with WebcamSource as parent
    data->video_sink = new QVideoSink(source);
    data->capture_session = new QMediaCaptureSession(source);
    data->capture_session->setCamera(data->camera);
    data->capture_session->setVideoSink(data->video_sink);

    Debug(QS("Successfully created Qt6 QCamera for <%s>", CSTR(device_id)));
    return true;
}

void
cleanup_QtCamera(WebcamSource::PlatformData *data)
{
    //Stop camera (Qt parent-child system handles deletion automatically)
    if (data->camera)
    {
        data->camera->stop();
    }
    //No manual delete needed - Qt handles cleanup when WebcamSource is destroyed
}

QImage
captureFrame_QtCamera(WebcamSource::PlatformData *data)
{
    //Qt6 QCamera frame capture
    if (!data->video_sink)
        return QImage();

    //Get current video frame from sink
    QVideoFrame frame = data->video_sink->videoFrame();
    if (!frame.isValid())
    {
        Debug(QS("No valid video frame available"));
        return QImage();
    }

    //Convert QVideoFrame to QImage
    QImage image = frame.toImage();
    if (image.isNull())
    {
        Debug(QS("Failed to convert video frame to image"));
        return QImage();
    }

    return image;
}

bool
startPreview_QtCamera(WebcamSource::PlatformData *data)
{
    //Start Qt6 QCamera for live streaming
    if (data->camera)
    {
        data->camera->start();
        return true;
    }
    return false;
}

void
stopPreview_QtCamera(WebcamSource::PlatformData *data)
{
    //Stop Qt6 QCamera
    if (data->camera)
    {
        data->camera->stop();
    }
}

#endif //USE_QTCAMERA
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

#include "scan/webcamsource.hpp"
#include "core/classlogger.hpp"

//Forward declarations for platform-specific implementations
#ifdef USE_GSTREAMER
extern QList<ScanDeviceInfo> enumerateDevices_GStreamer();
extern bool initialize_GStreamer(WebcamSource *source, const QString &device_id);
extern void cleanup_GStreamer(WebcamSource::PlatformData *data);
extern QImage captureFrame_GStreamer(WebcamSource::PlatformData *data);
extern bool startPreview_GStreamer(WebcamSource::PlatformData *data);
extern void stopPreview_GStreamer(WebcamSource::PlatformData *data);
#endif

#ifdef USE_QTCAMERA
extern QList<ScanDeviceInfo> enumerateDevices_QtCamera();
extern bool initialize_QtCamera(WebcamSource *source, const QString &device_id);
extern void cleanup_QtCamera(WebcamSource::PlatformData *data);
extern QImage captureFrame_QtCamera(WebcamSource::PlatformData *data);
extern bool startPreview_QtCamera(WebcamSource::PlatformData *data);
extern void stopPreview_QtCamera(WebcamSource::PlatformData *data);
#endif

WebcamSource::WebcamSource(const QString &device_identifier,
                           const QString &device_description,
                           QObject *parent)
            : ScanSource(parent),
              m_device_identifier(device_identifier),
              m_device_description(device_description),
              m_is_scanning(false),
              m_is_initialized(false),
              m_live_preview_active(false),
              m_preview_timer(0),
              m_frame_fail_count(0),
              m_platform_data(new PlatformData())
{
    m_capabilities.preview_mode = PreviewMode::LiveStream;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;
    m_capabilities.supports_scan_settings = false;  //Webcams don't need scan parameter controls

    m_preview_timer = new QTimer(this);
    m_preview_timer->setInterval(33); //~30 fps
    connect(m_preview_timer, SIGNAL(timeout()), this, SLOT(onPreviewTimer()));
}

WebcamSource::~WebcamSource()
{
    stopPreview();

#ifdef USE_GSTREAMER
    cleanup_GStreamer(m_platform_data.get());
#endif

#ifdef USE_QTCAMERA
    cleanup_QtCamera(m_platform_data.get());
#endif

    //unique_ptr automatically deletes PlatformData
}

QList<ScanDeviceInfo>
WebcamSource::enumerateDevices()
{
#ifdef USE_GSTREAMER
    return enumerateDevices_GStreamer();
#endif

#ifdef USE_QTCAMERA
    return enumerateDevices_QtCamera();
#endif

    return QList<ScanDeviceInfo>();
}

ScanCapabilities
WebcamSource::capabilities() const
{
    return m_capabilities;
}

QString
WebcamSource::deviceName() const
{
    return m_device_identifier;
}

QString
WebcamSource::deviceDescription() const
{
    return m_device_description;
}

bool
WebcamSource::initialize()
{
    if (m_is_initialized)
    {
        Debug(QS("<%s> already initialized", CSTR(m_device_identifier)));
        return true;
    }

    Debug(QS("Initializing <%s>...", CSTR(m_device_identifier)));

#ifdef USE_GSTREAMER
    if (initialize_GStreamer(this, m_device_identifier))
    {
        queryCapabilities();
        m_is_initialized = true;
        return true;
    }
#endif

#ifdef USE_QTCAMERA
    if (initialize_QtCamera(this, m_device_identifier))
    {
        queryCapabilities();
        m_is_initialized = true;
        return true;
    }
#endif

    return false;
}

bool
WebcamSource::startScan(const ScanParameters &params)
{
    if (!m_is_initialized)
        return false;
    
    m_is_scanning = true;
    emit scanStarted();
    
    //Capture a single frame
    QImage frame = captureFrame();
    
    if (!frame.isNull())
    {
        emit pageScanned(frame, 0);
        emit scanComplete();
    }
    else
    {
        emit scanError("Failed to capture frame from webcam");
    }
    
    m_is_scanning = false;
    return !frame.isNull();
}

void
WebcamSource::cancelScan()
{
    m_is_scanning = false;
}

bool
WebcamSource::isScanning() const
{
    return m_is_scanning;
}

void
WebcamSource::queryCapabilities()
{
    //Webcam capabilities are mostly fixed
    m_capabilities.supports_color_mode = true;
    m_capabilities.supported_resolutions << 640 << 1280 << 1920;
    m_capabilities.supported_color_modes << "Color";
}

QImage
WebcamSource::captureFrame()
{
#ifdef USE_GSTREAMER
    return captureFrame_GStreamer(m_platform_data.get());
#endif

#ifdef USE_QTCAMERA
    return captureFrame_QtCamera(m_platform_data.get());
#endif

    return QImage();
}

bool
WebcamSource::startPreview()
{
    if (!m_is_initialized)
    {
        Debug(QS("startPreview: not initialized"));
        return false;
    }

    if (m_live_preview_active)
        return true;

    Debug(QS("Starting live preview stream"));
    m_frame_fail_count = 0;

#ifdef USE_GSTREAMER
    if (!startPreview_GStreamer(m_platform_data.get()))
        return false;
#endif

#ifdef USE_QTCAMERA
    if (!startPreview_QtCamera(m_platform_data.get()))
        return false;
#endif

    m_live_preview_active = true;
    m_preview_timer->start();
    return true;
}

void
WebcamSource::stopPreview()
{
    if (!m_live_preview_active)
        return;

    Debug(QS("Stopping live preview stream"));
    m_preview_timer->stop();
    m_live_preview_active = false;

#ifdef USE_GSTREAMER
    stopPreview_GStreamer(m_platform_data.get());
#endif

#ifdef USE_QTCAMERA
    stopPreview_QtCamera(m_platform_data.get());
#endif
}

bool
WebcamSource::isPreviewActive() const
{
    return m_live_preview_active;
}

bool
WebcamSource::isOpen() const
{
    //Return true if device is properly initialized
    return m_is_initialized;
}

void
WebcamSource::onPreviewTimer()
{
    if (!m_live_preview_active)
        return;

    QImage frame = captureFrame();
    if (!frame.isNull())
    {
        m_frame_fail_count = 0;
        emit previewFrameReady(frame);
    }
    else
    {
        m_frame_fail_count++;
        //Stop preview after 10 consecutive failures (~330ms)
        if (m_frame_fail_count >= 10)
        {
            Debug(QS("Too many consecutive frame capture failures (%d), stopping preview", m_frame_fail_count));
            stopPreview();
            emit scanError(tr("Failed to capture frames from camera. The device may be in use by another application."));
        }
    }
}
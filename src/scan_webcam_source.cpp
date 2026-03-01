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

#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include "scan/webcam_source.hpp"
#include "scan/webcam_backend.hpp"
#include "core/classlogger.hpp"

//Forward declarations for platform-specific implementations
#ifdef USE_GSTREAMER
extern QList<ScanDeviceInfo> enumerateDevices_GStreamer();
extern std::unique_ptr<WebcamBackend> createWebcamBackend_GStreamer();
//Backend implementation is in scan_webcam_source_gstreamer.cpp
#endif

#ifdef USE_QTCAMERA
extern QList<ScanDeviceInfo> enumerateDevices_QtCamera();
extern std::unique_ptr<WebcamBackend> createWebcamBackend_QtCamera();
//Backend implementation is in scan_webcam_source_qtcamera.cpp
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
              m_frame_fail_count(0)
{
    m_capabilities.preview_mode = PreviewMode::LiveStream;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;
    m_capabilities.supports_scan_settings = false; //webcams don't need scan parameter controls

    //Prefer GStreamer if available, otherwise fall back to QtCamera
    const char *backend_name = "none";
#ifdef USE_GSTREAMER
    m_backend = createWebcamBackend_GStreamer();
    backend_name = "GStreamer";
#elif defined(USE_QTCAMERA)
    m_backend = createWebcamBackend_QtCamera();
    backend_name = "QtCamera";
#endif

    Debug(QS("WebcamSource: backend active = %s", backend_name));

    m_preview_timer = new QTimer(this);
    m_preview_timer->setInterval(33); //~30 fps, polling captureFrame() without forcing a hard realtime rate
    connect(m_preview_timer, SIGNAL(timeout()), this, SLOT(onPreviewTimer()));
}

WebcamSource::~WebcamSource()
{
    stopPreview();
    //Backend destructor handles cleanup
    m_backend.reset();
}

QList<ScanDeviceInfo>
WebcamSource::enumerateDevices()
{
#ifdef USE_GSTREAMER
    Debug(QS("Enumerating webcam devices using backend=GStreamer"));
    QList<ScanDeviceInfo> devices = enumerateDevices_GStreamer();
    Debug(QS("Webcam enumeration (GStreamer) returned %lld device(s)", static_cast<long long>(devices.size())));
    return devices;
#elif defined(USE_QTCAMERA)
    Debug(QS("Enumerating webcam devices using backend=QtCamera"));
    QList<ScanDeviceInfo> devices = enumerateDevices_QtCamera();
    Debug(QS("Webcam enumeration (QtCamera) returned %lld device(s)", static_cast<long long>(devices.size())));
    return devices;
#else
    Debug(QS("Webcam enumeration: no backend compiled in"));
    return QList<ScanDeviceInfo>();
#endif
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

    if (!m_backend)
    {
        Debug(QS("No webcam backend available for <%s>", CSTR(m_device_identifier)));
        return false;
    }

    Debug(QS("Initializing <%s>...", CSTR(m_device_identifier)));
#ifdef USE_GSTREAMER
    Debug(QS("WebcamSource::initialize: backend=GStreamer, device=<%s>", CSTR(m_device_identifier)));
#elif defined(USE_QTCAMERA)
    Debug(QS("WebcamSource::initialize: backend=QtCamera, device=<%s>", CSTR(m_device_identifier)));
#else
    Debug(QS("WebcamSource::initialize: backend=none, device=<%s>", CSTR(m_device_identifier)));
#endif

    if (!m_backend->initialize(m_device_identifier))
        return false;

    queryCapabilities();
    m_is_initialized = true;
    return true;
}

bool
WebcamSource::startScan(const ScanParameters &params)
{
    if (!m_is_initialized)
        return false;
    
    m_is_scanning = true;
    emit scanStarted();

    //Capture a single frame
    //QtCamera often has no frame until stream is started
    const bool was_preview_active = m_live_preview_active;
    if (!was_preview_active)
    {
        Debug(QS("startScan: starting webcam stream for single capture"));
        if (!startPreview())
            Debug(QS("startScan: startPreview() failed (continuing, capture may fail)"));
    }

    QImage frame;
    QElapsedTimer t;
    t.start();
    while (frame.isNull() && t.elapsed() < 1500)
    {
        //Allow queued multimedia events (e.g. QVideoSink::videoFrameChanged) to be delivered
        //Without this, QtCamera capture can starve while we block the GUI thread
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        frame = captureFrame();
        if (!frame.isNull())
            break;
        QThread::msleep(10);
    }

    if (!was_preview_active)
    {
        Debug(QS("startScan: stopping webcam stream after single capture"));
        stopPreview();
    }

    if (!frame.isNull())
    {
        qscan::ScanPageInfo info;
        info.backend_kind = "Webcam";
        emit pageScanned(frame, 0, info);
        emit scanComplete();
    }
    else
    {
        Debug(QS("startScan: failed to capture frame from <%s>", CSTR(m_device_identifier)));
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
    //Common webcam width presets
    m_capabilities.supported_resolutions << 640 << 1280 << 1920;
    m_capabilities.supported_color_modes << "Color";
}

QImage
WebcamSource::captureFrame()
{
    if (!m_backend)
        return QImage();

    return m_backend->captureFrame();
}

bool
WebcamSource::startPreview()
{
    if (!m_is_initialized)
    {
        Debug(QS("startPreview: not initialized"));
        return false;
    }

    if (!m_backend)
        return false;

    if (m_live_preview_active)
        return true;

    Debug(QS("Starting live preview stream"));
    m_frame_fail_count = 0;

    if (!m_backend->startPreview())
        return false;

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

    if (m_backend)
        m_backend->stopPreview();
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
    return m_is_initialized && (m_backend != nullptr);
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
        //Stop preview after 10 consecutive failures (~330ms at 33ms interval)
        if (m_frame_fail_count >= 10)
        {
            Debug(QS("Too many consecutive frame capture failures (%d), stopping preview", m_frame_fail_count));
            stopPreview();
            emit scanError(tr("Failed to capture frames from camera. The device may be in use by another application."));
        }
    }
}
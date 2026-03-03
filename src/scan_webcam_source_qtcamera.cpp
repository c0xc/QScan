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

#include <dlfcn.h>
#include <memory>

#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QImage>
#include <QMetaObject>
#include <QVideoFrameFormat>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

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
        //Resolve requested camera identifier
        QString raw_id = device_id;
        if (raw_id.startsWith(QStringLiteral("qcamera:")))
            raw_id = raw_id.mid(QStringLiteral("qcamera:").size());
        else if (raw_id.startsWith(QStringLiteral("qtcamera:")))
            raw_id = raw_id.mid(QStringLiteral("qtcamera:").size());

        //Locate matching Qt camera device
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

        //Reset backend state
        m_camera.reset();
        m_video_sink.reset();
        m_capture_session.reset();
        m_last_frame = QVideoFrame();
        m_frame_available = false;
        m_empty_frame_log_count = 0;
        m_device_id = device_id;

        //Create Qt capture objects and wire session
        m_camera.reset(new QCamera(selected_device));
        m_video_sink.reset(new QVideoSink());
        m_capture_session.reset(new QMediaCaptureSession());
        m_capture_session->setCamera(m_camera.get());
        m_capture_session->setVideoSink(m_video_sink.get());

        //Connect frame and diagnostics callbacks
        QObject::connect(
            m_video_sink.get(),
            &QVideoSink::videoFrameChanged,
            [this](const QVideoFrame &frame)
            {
                if (!frame.isValid())
                    return;

                //Store the frame itself, not the converted image
                //Conversion to QImage can deadlock if done from GStreamer thread context
                //when Qt Multimedia's internal pipeline lacks GL/RHI backend
                m_last_frame = frame;
                m_frame_available = true;
            }
        );

        QObject::connect(
            m_camera.get(),
            &QCamera::activeChanged,
            [this](bool active)
            {
                Debug(QS("QtCamera activeChanged: active=%d for <%s>", active ? 1 : 0, CSTR(m_device_id)));
            }
        );

        QObject::connect(
            m_camera.get(),
            &QCamera::errorOccurred,
            [this](QCamera::Error error, const QString &error_string)
            {
                Debug(QS("QtCamera errorOccurred: %d (%s) for <%s>", static_cast<int>(error), CSTR(error_string), CSTR(m_device_id)));
            }
        );

        Debug(QS("Successfully created Qt6 QCamera for <%s>", CSTR(device_id)));
        return true;
    }

    QImage
    captureFrame() override
    {
        //Return early when no frame is available
        if (!m_frame_available)
        {
            m_empty_frame_log_count++;
            if (m_empty_frame_log_count <= 3 || (m_empty_frame_log_count % 30) == 0)
                Debug(QS("No valid video frame available (count=%d)", m_empty_frame_log_count));
            return QImage();
        }

        //Convert frame on caller thread
        //Avoids callback-thread conversion deadlocks in broken Qt runtime setups
        const QImage img = m_last_frame.toImage();
        if (img.isNull())
        {
            if (m_empty_frame_log_count < 10)
            {
                const QVideoFrameFormat fmt = m_last_frame.surfaceFormat();
                Debug(QS("QtCamera frame conversion failed: size=%dx%d pixel_format=%d",
                         fmt.frameWidth(),
                         fmt.frameHeight(),
                         static_cast<int>(fmt.pixelFormat())));
                m_empty_frame_log_count++;
            }
            return QImage();
        }

        //Reset failure counter on success
        m_empty_frame_log_count = 0;
        return img;
    }

    bool
    startPreview() override
    {
        if (!m_camera)
            return false;

        //Reset preview frame state
        m_last_frame = QVideoFrame();
        m_frame_available = false;
        m_empty_frame_log_count = 0;

        //Queue camera start on Qt object thread
        Debug(QS("QtCamera start(): queueing start for <%s>", CSTR(m_device_id)));
        QMetaObject::invokeMethod(
            m_camera.get(),
            [this]()
            {
                if (m_camera)
                    m_camera->start();
            },
            Qt::QueuedConnection
        );
        return true;
    }

    void
    stopPreview() override
    {
        if (!m_camera)
            return;

        //Queue camera stop on Qt object thread
        QMetaObject::invokeMethod(
            m_camera.get(),
            [this]()
            {
                if (m_camera)
                    m_camera->stop();
            },
            Qt::QueuedConnection
        );
    }

private:
    //Qt backend state, auto-cleaned by unique_ptr
    QString m_device_id;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_capture_session;
    std::unique_ptr<QVideoSink> m_video_sink;
    QVideoFrame m_last_frame;
    bool m_frame_available = false;
    int m_empty_frame_log_count = 0;

};

static bool
isQtCameraPathFunctional()
{
    //Why this check exists
    //AppImage builds can bundle mismatched Qt/GL multimedia libs where camera enumeration looks empty
    //or preview conversion freezes although hardware exists

    //Validate QVideoSink construction used by QCamera capture session
    QVideoSink sink;

    //Validate offscreen GL context used by frame conversion paths
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    if (!context.create() || !context.makeCurrent(&surface))
        return false;

    //Validate frame-to-image conversion used by preview capture path
    QImage testImg(160, 120, QImage::Format_RGB32);
    testImg.fill(Qt::green);
    QVideoFrame testFrame(testImg);
    if (testFrame.toImage().isNull())
        return false;

    return true;
}

static QList<QCameraDevice>
queryQtCameraDevicesWithRetry()
{
    //Collect camera devices (first pass)
    QList<QCameraDevice> camera_devices = QMediaDevices::videoInputs();
    if (!camera_devices.isEmpty())
        return camera_devices;

    //Retry once after short event-loop delay
    //Allows plugin/device discovery to settle in slower AppImage runtimes
    Debug(QS("QtCamera enumeration first pass returned 0 device(s), retrying once"));
    QEventLoop loop;
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();

    camera_devices = QMediaDevices::videoInputs();
    Debug(QS("QtCamera enumeration retry returned %lld device(s)", static_cast<long long>(camera_devices.size())));
    return camera_devices;
}

static void
logQtCameraRuntimeContext()
{
    //Log runtime context for AppImage-specific diagnostics
    const QByteArray appdir = qgetenv("APPDIR");
    const QByteArray ld_library_path = qgetenv("LD_LIBRARY_PATH");
    const QByteArray qt_plugin_path = qgetenv("QT_PLUGIN_PATH");
    const QByteArray qt_qpa_platform = qgetenv("QT_QPA_PLATFORM");
    const QByteArray qt_debug_plugins = qgetenv("QT_DEBUG_PLUGINS");

    Debug(QS("QtCamera runtime context: APPDIR=<%s>", appdir.constData()));
    Debug(QS("QtCamera runtime context: QT_QPA_PLATFORM=<%s>", qt_qpa_platform.constData()));
    Debug(QS("QtCamera runtime context: QT_PLUGIN_PATH=<%s>", qt_plugin_path.constData()));
    Debug(QS("QtCamera runtime context: QT_DEBUG_PLUGINS=<%s>", qt_debug_plugins.constData()));
    Debug(QS("QtCamera runtime context: LD_LIBRARY_PATH=<%s>", ld_library_path.constData()));
}

static bool
isQtCameraDiagEnabled()
{
    //Read diagnostics toggle
    const QByteArray value = qgetenv("QSCAN_QTCAMERA_DIAG").trimmed().toLower();
    if (value.isEmpty() || value == "0" || value == "false" || value == "off" || value == "no")
        return false;
    return true;
}

static bool
probePluginDlopen(const QString &path, const char *label)
{
    //Validate plugin file presence
    if (!QFileInfo::exists(path))
    {
        Debug(QS("QtCamera plugin probe: missing <%s> at <%s>", label, CSTR(path)));
        return false;
    }

    //Probe plugin loadability and transitive dependencies
    void *handle = dlopen(path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char *err = dlerror();
        Debug(QS("QtCamera plugin probe: dlopen FAILED for <%s>: %s", label, err ? err : "unknown"));
        return false;
    }

    Debug(QS("QtCamera plugin probe: dlopen OK for <%s>", label));
    dlclose(handle);
    return true;
}

static bool
runQtCameraBackendDiagnostics()
{
    //Run AppImage-oriented backend diagnostics
    //Detects missing/broken Qt Multimedia backend plugins
    //
    //Original error this detects (AppImage with missing multimedia plugins):
    //"No QtMultimedia backends found. Only QMediaDevices, QAudioDevice, QSoundEffect, QAudioSink, and QAudioSource are available."
    //"Failed to create QVideoSink \"Not available\""
    //
    //Related freeze path (avoided by preferring FFmpeg backend in AppImage):
    //"qt.multimedia.gstreamer: Using Qt multimedia with GStreamer version: ..."
    //"QGstElement::getPipeline failed for element: videoConvert"
    //"QRhi* ... No RHI backend. Using CPU conversion."
    //"Complete freeze when accessing webcam (DMA-BUF video buffer mapping deadlocks without GL context)"

    //Resolve AppImage runtime root
    const QString appdir = QString::fromLocal8Bit(qgetenv("APPDIR"));
    if (appdir.isEmpty())
    {
        Debug(QS("QtCamera diagnostics skipped: APPDIR is unset"));
        return true;
    }

    //Build multimedia plugin paths
    const QString multimedia_dir = appdir + QStringLiteral("/usr/plugins/multimedia");
    const QString ffmpeg_plugin = multimedia_dir + QStringLiteral("/libffmpegmediaplugin.so");
    const QString gstreamer_plugin = multimedia_dir + QStringLiteral("/libgstreamermediaplugin.so");

    Debug(QS("QtCamera diagnostics: multimedia dir=<%s>", CSTR(multimedia_dir)));
    Debug(QS("QtCamera diagnostics: ffmpeg plugin exists=%d", QFileInfo::exists(ffmpeg_plugin) ? 1 : 0));
    Debug(QS("QtCamera diagnostics: gstreamer plugin exists=%d", QFileInfo::exists(gstreamer_plugin) ? 1 : 0));

    bool any_plugin_exists = false;
    bool any_plugin_loads = false;

    //Probe FFmpeg backend plugin
    if (QFileInfo::exists(ffmpeg_plugin))
    {
        any_plugin_exists = true;
        if (probePluginDlopen(ffmpeg_plugin, "ffmpeg"))
            any_plugin_loads = true;
    }

    //Probe GStreamer backend plugin
    if (QFileInfo::exists(gstreamer_plugin))
    {
        any_plugin_exists = true;
        if (probePluginDlopen(gstreamer_plugin, "gstreamer"))
            any_plugin_loads = true;
    }

    //Classify diagnostics result
    if (!any_plugin_exists)
    {
        Debug(QS("QtCamera diagnostics: no multimedia backend plugin found in AppImage"));
        return false;
    }

    if (!any_plugin_loads)
    {
        Debug(QS("QtCamera diagnostics: backend plugins present but none loadable"));
        return false;
    }

    Debug(QS("QtCamera diagnostics: at least one multimedia backend plugin is loadable"));
    return true;
}

static bool
isQtCameraDeepProbeEnabled()
{
    //Read deep probe toggle
    const QByteArray value = qgetenv("QSCAN_QTCAMERA_DEEP_PROBE").trimmed().toLower();
    if (value.isEmpty() || value == "0" || value == "false" || value == "off" || value == "no")
        return false;
    return true;
}

static bool
probeQtCameraStart(const QCameraDevice &device)
{
    //Create minimal QCamera capture chain
    //Used only for explicit diagnostics when AppImage runtime mismatches are suspected
    QCamera camera(device);
    QVideoSink sink;
    QMediaCaptureSession capture_session;
    capture_session.setCamera(&camera);
    capture_session.setVideoSink(&sink);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(500);

    bool active_seen = false;
    bool valid_frame_seen = false;

    //Connect camera activation callback
    QObject::connect(
        &camera,
        &QCamera::activeChanged,
        &loop,
        [&active_seen, &loop](bool active)
        {
            if (!active)
                return;
            active_seen = true;
            loop.quit();
        }
    );

    //Connect frame callback
    QObject::connect(
        &sink,
        &QVideoSink::videoFrameChanged,
        &loop,
        [&valid_frame_seen, &loop](const QVideoFrame &frame)
        {
            if (!frame.isValid())
                return;
            valid_frame_seen = true;
            loop.quit();
        }
    );

    //Connect timeout callback
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    //Run bounded start probe
    camera.start();
    timer.start();
    loop.exec();
    camera.stop();

    //Accept either activation or valid frame as success
    return active_seen || valid_frame_seen;
}

std::unique_ptr<WebcamBackend>
createWebcamBackend_QtCamera()
{
    return std::unique_ptr<WebcamBackend>(new QtCameraWebcamBackend());
}

bool
enumerateDevices_QtCamera(QList<ScanDeviceInfo> &devices)
{
    //Reset result container
    devices.clear();

    Debug(QS("Enumerating Qt6 QCamera devices..."));

    //Collect camera devices from Qt
    QList<QCameraDevice> camera_devices = queryQtCameraDevicesWithRetry();

    Debug(QS("Found %lld camera device(s)", static_cast<long long>(camera_devices.size())));

    //Distinguish empty hardware from runtime failure
    if (camera_devices.isEmpty())
    {
        //Log context before functional probe for easier AppImage triage
        logQtCameraRuntimeContext();

        //Run diagnostics in AppImage mode to detect missing/broken multimedia plugins
        //Qt Multimedia returns 0 cameras when backend plugins are missing/broken - this
        //distinguishes that failure mode from "no hardware present"
        //
        //This catches the common failure: "No QtMultimedia backends found"
        const QString appdir = QString::fromLocal8Bit(qgetenv("APPDIR"));
        if (!appdir.isEmpty())
        {
            if (!runQtCameraBackendDiagnostics())
            {
                Debug(QS("QtCamera enumeration failed: multimedia backend plugin load failed"));
                return false;
            }
        }

        if (!isQtCameraPathFunctional())
        {
            Debug(QS("QtCamera enumeration failed: QVideoSink/GL/frame conversion path broken"));
            return false;
        }
        Debug(QS("QtCamera enumeration: no cameras detected (backend OK, hardware absent)"));
        return true;
    }

    //Optionally deep-probe camera start path
    //Disabled by default to avoid side effects during normal enumeration
    if (isQtCameraDeepProbeEnabled())
    {
        Debug(QS("QtCamera deep probe enabled via QSCAN_QTCAMERA_DEEP_PROBE"));

        bool probe_ok = false;
        for (int i = 0; i < camera_devices.size(); ++i)
        {
            const QCameraDevice &cam_device = camera_devices.at(i);
            if (probeQtCameraStart(cam_device))
            {
                probe_ok = true;
                Debug(QS("QtCamera deep probe OK for camera index=%d", i));
                break;
            }
        }

        if (!probe_ok)
        {
            Debug(QS("QtCamera deep probe FAILED for all cameras"));
            return false;
        }
    }

    //Translate Qt camera records into scan device entries
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

    return true;
}

QList<ScanDeviceInfo>
enumerateDevices_QtCamera()
{
    QList<ScanDeviceInfo> devices;
    enumerateDevices_QtCamera(devices);
    return devices;
}

#endif //USE_QTCAMERA

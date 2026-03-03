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
//
//QtCamera reliability in AppImage with GStreamer multimedia plugin
//===================================================================
//Qt Multimedia's GStreamer backend (libgstreamermediaplugin.so) is unreliable
//inside AppImage when the GStreamer version on the host differs from the one
//the plugin was built against in the container. The plugin links against
//libgst*.so at build time but loads the host's GStreamer at runtime (because
//we deliberately keep GStreamer host-side to avoid bundling a full media
//framework). The ABI is nominally stable within GStreamer 1.x, but Qt's
//internal pipeline construction ("camerabin") depends on GL/RHI integration
//and DMA-BUF buffer negotiation details that change across GStreamer versions.
//
//Observed failure (container GStreamer 1.22, host GStreamer 1.26):
//  "QGstElement::getPipeline failed for element: videoConvert"
//  followed by an infinite deadlock in executeWhilePadsAreIdle() inside
//  Qt's QGstreamerMediaCaptureSession::setCameraActive().
//
//The freeze happens because Qt's camerabin-style pipeline fails to link
//its videoConvert element properly, then tries to wait for pad idle state
//on a broken element — with no timeout. This blocks the main thread.
//
//Why the AppImage relies on host GStreamer:
//GStreamer is a large framework with dozens of plugins; bundling it creates
//the same ABI coupling problem we already have with SANE (host drivers
//loaded via dlopen conflict with bundled libs). Keeping GStreamer host-side
//is the lesser evil — it just means Qt's GStreamer multimedia plugin must
//be compatible with whatever GStreamer the host has installed.
//GStreamer 1.x is available on essentially all desktop Linux distributions,
//so the host dependency is acceptable for a desktop scanner app.
//
//The preferred solution is the FFmpeg multimedia plugin
//(libffmpegmediaplugin.so), which is self-contained and has no host
//GStreamer dependency. It requires building Qt with FFmpeg-devel installed
//in the container. When both plugins are available, the AppImage script
//bundles FFmpeg and skips GStreamer.
//
//Freeze detection: the deep probe (probeQtCameraStart) cannot reliably
//detect this freeze from within the same process because camera.start()
//deadlocks synchronously before the event loop gets control, so the
//timeout timer never fires. Detection requires running the probe in a
//subprocess with a wall-clock timeout (see probeQtCameraStartSubprocess).
//->
//QtCamera deep probe: cached result is FAIL (camera freeze detected by preflight)
//Webcam enumeration (QtCamera) returned 0 device(s), success=0

#ifdef USE_QTCAMERA

#include <dlfcn.h>
#include <memory>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QCoreApplication>
#include <QElapsedTimer>

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

//==============================================================================
//Diagnostic helpers for QtCamera runtime validation
//==============================================================================

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

/*****************************************************************************
 * Deep probe: subprocess-based camera start test
 *
 * Detects the Qt GStreamer camerabin freeze by forking a child process
 * that attempts camera.start(). If the child hangs (the known failure mode
 * with mismatched GStreamer versions), the parent kills it after a timeout.
 *****************************************************************************/

static QString
deepProbeCachePath()
{
    //Cache dir follows XDG, same location as preflight.log
    const QString cache_dir = QString::fromLocal8Bit(
        qgetenv("XDG_CACHE_HOME").isEmpty()
            ? QByteArray(qgetenv("HOME") + "/.cache")
            : qgetenv("XDG_CACHE_HOME"))
        + QStringLiteral("/qscan");
    return cache_dir + QStringLiteral("/qtcamera-probe.cache");
}

static bool
writeDeepProbeCache(bool probe_ok)
{
    const QString path = deepProbeCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        Debug(QS("QtCamera deep probe cache: failed to write <%s>", CSTR(path)));
        return false;
    }

    //Format: appdir\nresult\n
    //Keyed by APPDIR so the cache invalidates when the AppImage changes
    const QByteArray appdir = qgetenv("APPDIR");
    f.write(appdir + "\n");
    f.write(probe_ok ? "ok\n" : "fail\n");
    f.close();

    Debug(QS("QtCamera deep probe cache: wrote result=%s to <%s>",
             probe_ok ? "ok" : "fail", CSTR(path)));
    return true;
}

enum DeepProbeCacheResult { CacheHit_OK, CacheHit_Fail, CacheMiss };

static DeepProbeCacheResult
readDeepProbeCache()
{
    const QString path = deepProbeCachePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return CacheMiss;

    const QByteArray content = f.readAll();
    f.close();

    const QList<QByteArray> lines = content.split('\n');
    if (lines.size() < 2)
        return CacheMiss;

    //Validate APPDIR matches
    const QByteArray cached_appdir = lines.at(0);
    const QByteArray current_appdir = qgetenv("APPDIR");
    if (cached_appdir != current_appdir)
    {
        Debug(QS("QtCamera deep probe cache: APPDIR mismatch, invalidating"));
        return CacheMiss;
    }

    const QByteArray result = lines.at(1).trimmed();
    if (result == "ok")
    {
        Debug(QS("QtCamera deep probe cache: hit (ok)"));
        return CacheHit_OK;
    }
    if (result == "fail")
    {
        Debug(QS("QtCamera deep probe cache: hit (fail)"));
        return CacheHit_Fail;
    }

    return CacheMiss;
}

static bool
probeQtCameraStartSubprocess()
{
    //Run the camera start probe in a forked subprocess with a wall-clock timeout.
    //This is the only reliable way to detect the Qt GStreamer camerabin freeze,
    //because the deadlock happens synchronously inside camera.start() before
    //any QTimer or event loop gets control in the same process.
    //
    //The forked child inherits the parent's X11/Wayland connection. If it
    //freezes and is killed, this can corrupt shared display state and cause
    //the parent's next GUI operation to hang. Therefore this function must
    //only be called from headless contexts (--self-check / preflight).
    //GUI processes should read the cached result via readDeepProbeCache().

    static const int kProbeTimeoutMs = 3000;

    Debug(QS("QtCamera deep probe: forking subprocess"));

    pid_t pid = fork();
    if (pid < 0)
    {
        Debug(QS("QtCamera deep probe: fork() failed"));
        return true; //assume OK if we can't probe
    }

    if (pid == 0)
    {
        //Child process: attempt camera start
        //Re-initialize Qt internals minimally after fork
        QList<QCameraDevice> devices = QMediaDevices::videoInputs();
        if (devices.isEmpty())
            _exit(2); //no cameras visible to child

        QCamera camera(devices.first());
        QVideoSink sink;
        QMediaCaptureSession session;
        session.setCamera(&camera);
        session.setVideoSink(&sink);

        //Set up a watchdog alarm in case start() blocks
        //SIGALRM default action is to terminate the process
        alarm(kProbeTimeoutMs / 1000 + 1);

        camera.start();

        //If we get here, start() did not freeze
        //Process a few events to let activeChanged arrive
        for (int i = 0; i < 10; ++i)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (camera.isActive())
                break;
        }

        camera.stop();
        _exit(camera.isActive() ? 0 : 1);
    }

    //Parent process: wait with timeout
    QElapsedTimer elapsed;
    elapsed.start();

    while (elapsed.elapsed() < kProbeTimeoutMs)
    {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
        {
            if (WIFEXITED(status))
            {
                int code = WEXITSTATUS(status);
                bool ok = (code == 0);
                Debug(QS("QtCamera deep probe: child exited with code %d", code));
                writeDeepProbeCache(ok);
                return ok;
            }
            Debug(QS("QtCamera deep probe: child terminated abnormally"));
            writeDeepProbeCache(false);
            return false;
        }

        //Brief sleep to avoid busy-waiting
        usleep(50000); //50ms
    }

    //Timeout: child is frozen, kill it
    Debug(QS("QtCamera deep probe: child did not exit within %dms, killing (freeze detected)", kProbeTimeoutMs));
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    writeDeepProbeCache(false);
    return false;
}

/*****************************************************************************
 * Backend factory and device enumeration
 *****************************************************************************/

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

    //Deep-probe: detect the Qt GStreamer camerabin freeze
    //
    //The freeze happens when Qt Multimedia's GStreamer plugin tries to start
    //a camera pipeline and deadlocks inside executeWhilePadsAreIdle().
    //Detection requires forking a subprocess (the deadlock is synchronous
    //and blocks the event loop). However, fork() in a GUI process corrupts
    //shared X11/Wayland state, so the actual probe only runs during the
    //headless preflight (QSCAN_PREFLIGHT_RUNNING=1 or --self-check).
    //The GUI process reads the cached result from the preflight run.
    {
        const bool in_appimage = !qgetenv("APPDIR").isEmpty();
        const QString appdir = QString::fromLocal8Bit(qgetenv("APPDIR"));
        const bool gst_plugin_only = in_appimage
            && !QFileInfo::exists(appdir + QStringLiteral("/usr/plugins/multimedia/libffmpegmediaplugin.so"))
            && QFileInfo::exists(appdir + QStringLiteral("/usr/plugins/multimedia/libgstreamermediaplugin.so"));
        const bool should_probe = isQtCameraDeepProbeEnabled() || gst_plugin_only;

        if (should_probe)
        {
            //Check cached result first (written by preflight or previous probe)
            DeepProbeCacheResult cached = readDeepProbeCache();
            if (cached == CacheHit_Fail)
            {
                Debug(QS("QtCamera deep probe: cached result is FAIL (camera freeze detected by preflight)"));
                return false;
            }
            if (cached == CacheHit_OK)
            {
                Debug(QS("QtCamera deep probe: cached result is OK"));
                //Fall through to normal enumeration
            }
            else
            {
                //No cache — run the probe only if we are in headless/preflight context
                const bool is_preflight = qEnvironmentVariableIsSet("QSCAN_PREFLIGHT_RUNNING");
                if (is_preflight || isQtCameraDeepProbeEnabled())
                {
                    Debug(QS("QtCamera deep probe: no cache, running subprocess probe (preflight=%d, env_override=%d)",
                              is_preflight ? 1 : 0,
                              isQtCameraDeepProbeEnabled() ? 1 : 0));

                    if (!probeQtCameraStartSubprocess())
                    {
                        Debug(QS("QtCamera deep probe FAILED: camera start froze or failed"));
                        return false;
                    }
                    Debug(QS("QtCamera deep probe OK: camera start succeeded"));
                }
                else
                {
                    //GUI process, no cache, no preflight ran — skip probe
                    //The camera will appear available; if it freezes, user loses this session
                    //but subsequent launches will have the preflight cache
                    Debug(QS("QtCamera deep probe: skipping (GUI context, no cached result)"));
                }
            }
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

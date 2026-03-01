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

//GStreamer webcam capture backend
#ifdef USE_GSTREAMER

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <memory>

#include <QDir>
#include <QFile>

#include "scan/webcam_backend.hpp"
#include "scan/scan_device_info.hpp"
#include "core/classlogger.hpp"

//Custom deleters for GStreamer objects
struct GstPipelineDeleter
{
    void
    operator()(GstElement *pipeline)
    {
        if (pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
    }
};

struct GstElementDeleter
{
    void
    operator()(GstElement *element)
    {
        if (element)
        {
            gst_object_unref(element);
        }
    }
};

static void
drainGStreamerBusForDebug(GstElement *pipeline)
{
    if (!pipeline)
        return;

    //Acquire bus
    //Some failures only surface on the bus, not via return values
    GstBus *bus = gst_element_get_bus(pipeline);
    if (!bus)
        return;

    //Drain error and warning messages
    for (;;)
    {
        GstMessage *msg = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));
        if (!msg)
            break;

        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
        {
            //Log error payload
            GError *err = 0;
            gchar *dbg = 0;
            gst_message_parse_error(msg, &err, &dbg);
            Debug(QS("GStreamer bus ERROR: %s", err ? err->message : "unknown"));
            if (dbg)
                Debug(QS("GStreamer bus ERROR debug: %s", dbg));
            if (err)
                g_error_free(err);
            if (dbg)
                g_free(dbg);
        }
        else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_WARNING)
        {
            //Log warning payload
            GError *err = 0;
            gchar *dbg = 0;
            gst_message_parse_warning(msg, &err, &dbg);
            Debug(QS("GStreamer bus WARNING: %s", err ? err->message : "unknown"));
            if (dbg)
                Debug(QS("GStreamer bus WARNING debug: %s", dbg));
            if (err)
                g_error_free(err);
            if (dbg)
                g_free(dbg);
        }

        gst_message_unref(msg);
    }

    //Cleanup
    gst_object_unref(bus);
}

static GstCaps *
createPreferredAppsinkCaps()
{
    //Preferred appsink raw formats
    //Keep the list small and QImage-friendly, let negotiation pick the first available
    return gst_caps_from_string(
        "video/x-raw,format=(string)BGRx;"
        "video/x-raw,format=(string)BGRA;"
        "video/x-raw,format=(string)RGBA;"
        "video/x-raw,format=(string)RGBx;"
        "video/x-raw,format=(string)RGB;"
        "video/x-raw,format=(string)BGR"
    );
}

class GStreamerWebcamBackend : public WebcamBackend
{
public:

    GStreamerWebcamBackend()
        : m_pipeline(nullptr),
          m_sink(nullptr),
          m_gst_initialized(false)
    {
    }

    ~GStreamerWebcamBackend() override
    {
        //State cleanup happens via smart-pointer deleters
        m_sink.reset();
        m_pipeline.reset();
    }

    bool
    initialize(const QString &device_id) override
    {
        //Init GStreamer once
        //Avoid repeated init across preview sessions
        if (!m_gst_initialized)
        {
            GError *error = 0;
            if (!gst_init_check(0, 0, &error))
            {
                Debug(QS("gst_init_check() FAILED: %s", error ? error->message : "unknown error"));
                if (error)
                    g_error_free(error);
                return false;
            }
            m_gst_initialized = true;
            Debug(QS("GStreamer initialized successfully"));
        }

        //Build v4l2src -> videoconvert -> appsink
        //Prefer appsink caps negotiation over hard-forcing a format in the pipeline string
        QString pipeline_str = QString("v4l2src device=%1 ! videoconvert ! appsink name=sink").arg(device_id);

        //Create pipeline
        GError *error = 0;
        GstElement *pipeline_raw = gst_parse_launch(pipeline_str.toUtf8().constData(), &error);
        if (!pipeline_raw)
        {
            Debug(QS("FAILED to create GStreamer pipeline: %s", error ? error->message : "unknown error"));
            if (error)
                g_error_free(error);
            return false;
        }
        //gst_parse_launch may set error even on success (warnings)
        if (error)
        {
            Debug(QS("GStreamer pipeline warning: %s", error->message));
            g_error_free(error);
        }
        m_pipeline.reset(pipeline_raw);

        //Get appsink element
        GstElement *sink_raw = gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "sink");
        if (!sink_raw)
        {
            Debug(QS("FAILED to get appsink element from pipeline"));
            m_pipeline.reset();
            return false;
        }
        m_sink.reset(sink_raw);

        //Request QImage-friendly raw formats
        GstCaps *preferred_caps = createPreferredAppsinkCaps();
        if (preferred_caps)
        {
            gst_app_sink_set_caps(GST_APP_SINK(m_sink.get()), preferred_caps);
            gst_caps_unref(preferred_caps);
        }

        //Appsink buffering policy
        //Drop old frames to avoid unbounded buffering when UI renders slower than capture
        g_object_set(m_sink.get(), "max-buffers", 1, "drop", TRUE, "sync", FALSE, NULL);

        //Preroll validation
        //Avoid returning success for a pipeline that will never produce samples
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED);
        if (ret == GST_STATE_CHANGE_FAILURE)
        {
            Debug(QS("GStreamer pipeline failed to enter PAUSED state"));
            drainGStreamerBusForDebug(m_pipeline.get());
            m_sink.reset();
            m_pipeline.reset();
            return false;
        }
        GstState state = GST_STATE_NULL;
        ret = gst_element_get_state(m_pipeline.get(), &state, 0, 2 * GST_SECOND);
        if (ret == GST_STATE_CHANGE_FAILURE || (state != GST_STATE_PAUSED && state != GST_STATE_PLAYING))
        {
            Debug(QS("GStreamer pipeline did not preroll (state=%d)", static_cast<int>(state)));
            drainGStreamerBusForDebug(m_pipeline.get());
            m_sink.reset();
            m_pipeline.reset();
            return false;
        }

        Debug(QS("Successfully created GStreamer pipeline for <%s>", CSTR(device_id)));
        return true;
    }

    QImage
    captureFrame() override
    {
        if (!m_pipeline || !m_sink)
            return QImage();

        GstState state;
        //Short state wait for UI responsiveness
        gst_element_get_state(m_pipeline.get(), &state, 0, 100 * GST_MSECOND);
        if (state != GST_STATE_PLAYING)
        {
            gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
        }

        //Pull a single frame with timeout
        //Avoid hanging the preview loop when the camera stops producing samples
        GstSample *sample = gst_app_sink_try_pull_sample(
            GST_APP_SINK(m_sink.get()), GST_SECOND
        );
        if (!sample)
        {
            Debug(QS("Failed to pull sample from appsink"));
            return QImage();
        }

        //Map sample buffer
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        {
            gst_sample_unref(sample);
            return QImage();
        }

        //Read negotiated caps into GstVideoInfo
        GstCaps *caps = gst_sample_get_caps(sample);
        GstVideoInfo video_info;
        if (!caps || !gst_video_info_from_caps(&video_info, caps))
        {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            Debug(QS("Failed to get video info from caps"));
            return QImage();
        }

        int width = GST_VIDEO_INFO_WIDTH(&video_info);
        int height = GST_VIDEO_INFO_HEIGHT(&video_info);
        int stride = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0);

        //Translate GStreamer pixel format to QImage format
        QImage::Format image_format = QImage::Format_Invalid;
        switch (GST_VIDEO_INFO_FORMAT(&video_info))
        {
            case GST_VIDEO_FORMAT_BGRx:
                //BGRx maps directly to Qt's 32-bit RGB on little-endian
                image_format = QImage::Format_RGB32;
                break;
            case GST_VIDEO_FORMAT_BGRA:
                //Qt ARGB32 is stored as BGRA in memory on little-endian
                image_format = QImage::Format_ARGB32;
                break;
            case GST_VIDEO_FORMAT_RGBA:
                image_format = QImage::Format_RGBA8888;
                break;
            case GST_VIDEO_FORMAT_RGBx:
                image_format = QImage::Format_RGBX8888;
                break;
            case GST_VIDEO_FORMAT_RGB:
                image_format = QImage::Format_RGB888;
                break;
            case GST_VIDEO_FORMAT_BGR:
                image_format = QImage::Format_BGR888;
                break;
            default:
                break;
        }

        if (image_format == QImage::Format_Invalid)
        {
            Debug(QS("Unsupported GStreamer pixel format in sample: %s", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&video_info))));
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            return QImage();
        }

        //Copy mapped buffer into owned QImage
        //Mapped data becomes invalid after unmap and sample unref
        QImage frame(
            (uchar*)map.data, width, height, stride,
            image_format
        );
        QImage copy = frame.copy();

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return copy;
    }

    bool
    startPreview() override
    {
        if (m_pipeline)
        {
            gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
            return true;
        }
        return false;
    }

    void
    stopPreview() override
    {
        if (m_pipeline)
        {
            gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED);
        }
    }

private:

    //Backend state, auto-cleaned up by deleters
    std::unique_ptr<GstElement, GstPipelineDeleter> m_pipeline;
    std::unique_ptr<GstElement, GstElementDeleter> m_sink;
    bool m_gst_initialized;

};

std::unique_ptr<WebcamBackend>
createWebcamBackend_GStreamer()
{
    return std::unique_ptr<WebcamBackend>(new GStreamerWebcamBackend());
}

QList<ScanDeviceInfo>
enumerateDevices_GStreamer()
{
    QList<ScanDeviceInfo> devices;
    
    Debug(QS("Enumerating GStreamer video devices..."));

    //Initialize GStreamer if not already done
    //Enumeration can be called without any active backend instance
    if (!gst_is_initialized())
    {
        GError *error = 0;
        if (!gst_init_check(0, 0, &error))
        {
            Debug(QS("gst_init_check() FAILED: %s", error ? error->message : "unknown error"));
            if (error)
                g_error_free(error);
            return devices;
        }
        Debug(QS("GStreamer initialized successfully"));
    }

    //Device monitor setup
    GstDeviceMonitor *monitor = gst_device_monitor_new();
    if (!monitor)
    {
        Debug(QS("Failed to create GstDeviceMonitor"));
        return devices;
    }

    //Filter for video sources
    GstCaps *caps = gst_caps_new_empty_simple("video/x-raw");
    gst_device_monitor_add_filter(monitor, "Video/Source", caps);
    gst_caps_unref(caps);

    //Start monitor
    //On failure, dump env vars that affect plugin discovery
    if (!gst_device_monitor_start(monitor))
    {
        Debug(QS("Failed to start GstDeviceMonitor"));
        const QByteArray gst_plugin_path = qgetenv("GST_PLUGIN_PATH");
        const QByteArray gst_plugin_system_path = qgetenv("GST_PLUGIN_SYSTEM_PATH_1_0");
        const QByteArray gst_plugin_scanner = qgetenv("GST_PLUGIN_SCANNER");
        const QByteArray gst_registry = qgetenv("GST_REGISTRY");
        Debug(QS("GST_PLUGIN_PATH=<%s>", gst_plugin_path.constData()));
        Debug(QS("GST_PLUGIN_SYSTEM_PATH_1_0=<%s>", gst_plugin_system_path.constData()));
        Debug(QS("GST_PLUGIN_SCANNER=<%s>", gst_plugin_scanner.constData()));
        Debug(QS("GST_REGISTRY=<%s>", gst_registry.constData()));

        GstElementFactory *v4l2src_factory = gst_element_factory_find("v4l2src");
        Debug(QS("gst_element_factory_find(v4l2src) = %s", v4l2src_factory ? "FOUND" : "NOT FOUND"));
        if (v4l2src_factory)
            gst_object_unref(v4l2src_factory);
        gst_object_unref(monitor);
        return devices;
    }

    //Enumerate devices
    GList *device_list = gst_device_monitor_get_devices(monitor);
    int device_count = 0;
    for (GList *item = device_list; item != 0; item = item->next)
    {
        GstDevice *device = GST_DEVICE(item->data);
        gchar *name = gst_device_get_display_name(device);
        gchar *device_class = gst_device_get_device_class(device);

        //Resolve device identifier
        //Prefer an actual /dev/videoN path when the driver exposes it
        GstStructure *props = gst_device_get_properties(device);
        QString identifier;
        if (props)
        {
            const gchar *device_path = gst_structure_get_string(props, "device.path");
            if (device_path)
            {
                identifier = QString::fromUtf8(device_path);
                Debug(QS("Found device path from properties: <%s>", device_path));
            }
            gst_structure_free(props);
        }
        
        //Fallback: try /dev/videoN by index
        if (identifier.isEmpty())
        {
            QString fallback_path = QString("/dev/video%1").arg(device_count);
            QFile test_file(fallback_path);
            if (test_file.exists())
            {
                identifier = fallback_path;
                Debug(QS("Using fallback device path: <%s>", CSTR(fallback_path)));
            }
            else
            {
                //Last resort: placeholder identifier
                identifier = QString("gstreamer:%1").arg(device_count);
                Debug(QS("WARNING: Could not determine device path, using placeholder: <%s>", CSTR(identifier)));
            }
        }

        QString desc = QString::fromUtf8(name ? name : "Unknown Camera");

        Debug(QS("Found GStreamer device [%d]: name=<%s>, class=<%s>, identifier=<%s>",
                 device_count, name ? name : "NULL", device_class ? device_class : "NULL", CSTR(identifier)));

        ScanDeviceInfo info(identifier, desc, ScanDeviceType::CAMERA);
        devices.append(info);

        g_free(name);
        g_free(device_class);
        gst_object_unref(device);
        device_count++;
    }
    g_list_free(device_list);

    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    Debug(QS("Enumeration complete, found %lld GStreamer webcam(s)", static_cast<long long>(devices.size())));
    
    return devices;
}

#endif //USE_GSTREAMER
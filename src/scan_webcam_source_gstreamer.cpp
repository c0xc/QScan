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

//GStreamer implementation for webcam capture
#ifdef USE_GSTREAMER

#include "scan/webcam_backend.hpp"
#include "scan/scan_device_info.hpp"
#include "core/classlogger.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QDir>
#include <QFile>
#include <memory>

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

        //Build pipeline: v4l2src->videoconvert->RGB->appsink
        QString pipeline_str = QString("v4l2src device=%1 ! videoconvert ! video/x-raw,format=RGB ! appsink name=sink").arg(device_id);

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

        GstElement *sink_raw = gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "sink");
        if (!sink_raw)
        {
            Debug(QS("FAILED to get appsink element from pipeline"));
            m_pipeline.reset();
            return false;
        }
        m_sink.reset(sink_raw);

        //Drop old frames, keep latest
        g_object_set(m_sink.get(), "max-buffers", 1, "drop", TRUE, NULL);

        Debug(QS("Successfully created GStreamer pipeline for <%s>", CSTR(device_id)));
        return true;
    }

    QImage
    captureFrame() override
    {
        if (!m_pipeline || !m_sink)
            return QImage();

        GstState state;
        gst_element_get_state(m_pipeline.get(), &state, 0, 100 * GST_MSECOND);
        if (state != GST_STATE_PLAYING)
        {
            gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
        }

        GstSample *sample = gst_app_sink_try_pull_sample(
            GST_APP_SINK(m_sink.get()), GST_SECOND
        );
        if (!sample)
        {
            Debug(QS("Failed to pull sample from appsink"));
            return QImage();
        }

        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        {
            gst_sample_unref(sample);
            return QImage();
        }

        GstCaps *caps = gst_sample_get_caps(sample);
        GstStructure *s = gst_caps_get_structure(caps, 0);
        int width, height;
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        QImage frame(
            (uchar*)map.data, width, height,
            QImage::Format_RGB888
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

    //Backend state,auto-cleaned up by deleters
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

    //Use GStreamer device monitor to enumerate video sources
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

    if (!gst_device_monitor_start(monitor))
    {
        Debug(QS("Failed to start GstDeviceMonitor"));
        gst_object_unref(monitor);
        return devices;
    }

    GList *device_list = gst_device_monitor_get_devices(monitor);
    int device_count = 0;
    for (GList *item = device_list; item != 0; item = item->next)
    {
        GstDevice *device = GST_DEVICE(item->data);
        gchar *name = gst_device_get_display_name(device);
        gchar *device_class = gst_device_get_device_class(device);

        //Try to get the actual device path from properties
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
        
        //Fallback: try to find /dev/videoN by index
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
                //Last resort: use index-based identifier (will fail at init)
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

    Debug(QS("Enumeration complete, found %d GStreamer webcam(s)", devices.size()));
    
    return devices;
}

#endif //USE_GSTREAMER
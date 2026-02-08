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

// GStreamer implementation (cross-platform, preferred if available)
#ifdef USE_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QDir>
#endif

// Linux V4L2 implementation (fallback for Linux when GStreamer not available)
#if defined(__linux__) && !defined(USE_GSTREAMER)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <QDir>
#include <errno.h>
#include <string.h>

// Number of buffers for V4L2 streaming
#define V4L2_BUFFER_COUNT 4
#endif

//Platform-specific data structure
#ifdef USE_GSTREAMER
struct WebcamSource::PlatformData
{
    GstElement *pipeline;
    GstElement *source;
    GstElement *sink;
    bool gst_initialized;

    PlatformData()
        : pipeline(0), source(0), sink(0), gst_initialized(false)
    {}
};
#elif defined(__linux__)
struct V4L2Buffer
{
    void *start;
    size_t length;
};

struct WebcamSource::PlatformData
{
    int fd;
    V4L2Buffer buffers[V4L2_BUFFER_COUNT];
    int buffer_count;
    bool streaming;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;

    PlatformData()
        : fd(-1), buffer_count(0), streaming(false),
          width(640), height(480), pixel_format(0)
    {
        for (int i = 0; i < V4L2_BUFFER_COUNT; i++)
        {
            buffers[i].start = 0;
            buffers[i].length = 0;
        }
    }
};
#else
struct WebcamSource::PlatformData
{
    PlatformData() {}
};
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
              m_platform_data(new PlatformData())
{
    m_capabilities.preview_mode = PreviewMode::LiveStream;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;

    m_preview_timer = new QTimer(this);
    m_preview_timer->setInterval(33); //~30 fps
    connect(m_preview_timer, SIGNAL(timeout()), this, SLOT(onPreviewTimer()));
}

WebcamSource::~WebcamSource()
{
    stopLivePreview();

#ifdef USE_GSTREAMER
    if (m_platform_data->pipeline)
    {
        gst_element_set_state(m_platform_data->pipeline, GST_STATE_NULL);
        gst_object_unref(m_platform_data->pipeline);
    }
#elif defined(__linux__)
    // Stop streaming if active
    if (m_platform_data->streaming && m_platform_data->fd >= 0)
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(m_platform_data->fd, VIDIOC_STREAMOFF, &type);
    }

    // Unmap buffers
    for (int i = 0; i < m_platform_data->buffer_count; i++)
    {
        if (m_platform_data->buffers[i].start)
        {
            munmap(m_platform_data->buffers[i].start,
                   m_platform_data->buffers[i].length);
        }
    }

    if (m_platform_data->fd >= 0)
    {
        close(m_platform_data->fd);
    }
#endif
    delete m_platform_data;
}

QList<ScanDeviceInfo>
WebcamSource::enumerateDevices()
{
    QList<ScanDeviceInfo> devices;

#ifdef USE_GSTREAMER
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

    // Use GStreamer device monitor to enumerate video sources
    GstDeviceMonitor *monitor = gst_device_monitor_new();
    if (!monitor)
    {
        Debug(QS("Failed to create GstDeviceMonitor"));
        return devices;
    }

    // Filter for video sources
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

        // Use device path as identifier (fallback to name if not available)
        QString identifier = QString("gstreamer:%1").arg(device_count);
        QString desc = QString::fromUtf8(name ? name : "Unknown Camera");

        Debug(QS("Found GStreamer device [%d]: name=<%s>, class=<%s>",
                 device_count, name ? name : "NULL", device_class ? device_class : "NULL"));

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

#elif defined(__linux__)
    Debug(QS("Enumerating V4L2 devices..."));

    // Scan /dev/video* devices
    QDir dev_dir("/dev");
    QStringList video_devices = dev_dir.entryList(QStringList() << "video*", QDir::System);

    Debug(QS("Found %d /dev/video* entries", video_devices.size()));

    foreach (const QString &device_file, video_devices)
    {
        QString device_path = "/dev/" + device_file;

        // Try to open and query capabilities
        int fd = open(device_path.toLocal8Bit().constData(), O_RDWR);
        if (fd < 0)
        {
            Debug(QS("Cannot open <%s>: errno=%d (%s)",
                     CSTR(device_path), errno, strerror(errno)));
            continue;
        }

        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
        {
            // Use device_caps if available (more accurate), otherwise use capabilities
            __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                       ? cap.device_caps : cap.capabilities;

            // Must be a video capture device with streaming support
            bool is_capture = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
            bool has_streaming = (caps & V4L2_CAP_STREAMING) != 0;
            // Skip metadata-only devices (V4L2_CAP_META_CAPTURE = 0x00800000)
            bool is_metadata = (caps & 0x00800000) != 0;

            if (is_capture && has_streaming && !is_metadata)
            {
                QString desc = QString::fromLatin1((const char*)cap.card);
                if (desc.isEmpty())
                    desc = device_file;

                Debug(QS("Found webcam: <%s>, card=<%s>, driver=<%s>, caps=0x%x",
                         CSTR(device_path), cap.card, cap.driver, caps));

                ScanDeviceInfo info(device_path, desc, ScanDeviceType::CAMERA);
                devices.append(info);
            }
            else
            {
                Debug(QS("Skipping <%s>: capture=%d, streaming=%d, metadata=%d (caps=0x%x)",
                         CSTR(device_path), is_capture, has_streaming, is_metadata, caps));
            }
        }
        else
        {
            Debug(QS("VIDIOC_QUERYCAP failed for <%s>: errno=%d (%s)",
                     CSTR(device_path), errno, strerror(errno)));
        }

        close(fd);
    }

    Debug(QS("Enumeration complete, found %d webcam(s)", devices.size()));
#endif

    return devices;
}

ScanCapabilities
WebcamSource::capabilities() const
{
    return m_capabilities;
}

QStringList
WebcamSource::supportedFormats() const
{
    return QStringList() << "JPG" << "PNG";
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
    //Initialize GStreamer if not already done
    if (!m_platform_data->gst_initialized)
    {
        GError *error = 0;
        if (!gst_init_check(0, 0, &error))
        {
            Debug(QS("gst_init_check() FAILED: %s", error ? error->message : "unknown error"));
            if (error)
                g_error_free(error);
            return false;
        }
        m_platform_data->gst_initialized = true;
        Debug(QS("GStreamer initialized successfully"));
    }

    //Create pipeline for capturing: v4l2src ! videoconvert ! appsink
    QString pipeline_str = QString("v4l2src device=%1 ! videoconvert ! appsink name=sink").arg(m_device_identifier);

    GError *error = 0;
    m_platform_data->pipeline = gst_parse_launch(pipeline_str.toUtf8().constData(), &error);
    if (!m_platform_data->pipeline)
    {
        Debug(QS("FAILED to create GStreamer pipeline: %s", error ? error->message : "unknown error"));
        if (error)
            g_error_free(error);
        return false;
    }

    //Get appsink element
    m_platform_data->sink = gst_bin_get_by_name(GST_BIN(m_platform_data->pipeline), "sink");
    if (!m_platform_data->sink)
    {
        Debug(QS("FAILED to get appsink element from pipeline"));
        gst_object_unref(m_platform_data->pipeline);
        m_platform_data->pipeline = 0;
        return false;
    }

    Debug(QS("Successfully created GStreamer pipeline for <%s>", CSTR(m_device_identifier)));
    queryCapabilities();
    m_is_initialized = true;
    return true;

#elif defined(__linux__)
    // Open V4L2 device
    m_platform_data->fd = open(m_device_identifier.toLocal8Bit().constData(), O_RDWR);
    if (m_platform_data->fd < 0)
    {
        Debug(QS("FAILED to open <%s>: errno=%d (%s)",
                 CSTR(m_device_identifier), errno, strerror(errno)));
        return false;
    }

    Debug(QS("Successfully opened <%s> (fd=%d)", CSTR(m_device_identifier), m_platform_data->fd));

    // Set video format - try MJPEG first, then YUYV
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 1280;
    fmt.fmt.pix.height = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(m_platform_data->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        Debug(QS("MJPEG format not supported, trying YUYV..."));
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (ioctl(m_platform_data->fd, VIDIOC_S_FMT, &fmt) < 0)
        {
            Debug(QS("VIDIOC_S_FMT failed: errno=%d (%s)", errno, strerror(errno)));
            close(m_platform_data->fd);
            m_platform_data->fd = -1;
            return false;
        }
    }

    m_platform_data->width = fmt.fmt.pix.width;
    m_platform_data->height = fmt.fmt.pix.height;
    m_platform_data->pixel_format = fmt.fmt.pix.pixelformat;

    Debug(QS("Format set: %dx%d, pixelformat=0x%x",
             m_platform_data->width, m_platform_data->height,
             m_platform_data->pixel_format));

    // Request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_platform_data->fd, VIDIOC_REQBUFS, &req) < 0)
    {
        Debug(QS("VIDIOC_REQBUFS failed: errno=%d (%s)", errno, strerror(errno)));
        close(m_platform_data->fd);
        m_platform_data->fd = -1;
        return false;
    }

    if (req.count < 2)
    {
        Debug(QS("Insufficient buffer memory"));
        close(m_platform_data->fd);
        m_platform_data->fd = -1;
        return false;
    }

    // Map buffers
    for (unsigned int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(m_platform_data->fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            Debug(QS("VIDIOC_QUERYBUF failed for buffer %d", i));
            continue;
        }

        m_platform_data->buffers[i].length = buf.length;
        m_platform_data->buffers[i].start = mmap(0, buf.length,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_SHARED,
                                                  m_platform_data->fd,
                                                  buf.m.offset);

        if (m_platform_data->buffers[i].start == MAP_FAILED)
        {
            Debug(QS("mmap failed for buffer %d", i));
            m_platform_data->buffers[i].start = 0;
            continue;
        }

        m_platform_data->buffer_count++;
    }

    Debug(QS("Mapped %d buffers", m_platform_data->buffer_count));

    queryCapabilities();
    m_is_initialized = true;
    return true;

#else
    Debug(QS("Webcam support not available on this platform"));
    return false;
#endif
}

bool
WebcamSource::startScan(const ScanParameters &params)
{
    if (!m_is_initialized)
        return false;
    
    m_is_scanning = true;
    emit scanStarted();
    
    // Capture a single frame
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
    // Webcam capabilities are mostly fixed
    m_capabilities.supports_color_mode = true;
    m_capabilities.supported_resolutions << 640 << 1280 << 1920;
    m_capabilities.supported_color_modes << "Color";
}

QImage
WebcamSource::captureFrame()
{
#if defined(__linux__) && !defined(USE_GSTREAMER)
    if (m_platform_data->fd < 0 || m_platform_data->buffer_count == 0)
    {
        Debug(QS("captureFrame: device not ready"));
        return QImage();
    }

    // Start streaming if not already
    if (!m_platform_data->streaming)
    {
        // Queue all buffers
        for (int i = 0; i < m_platform_data->buffer_count; i++)
        {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (ioctl(m_platform_data->fd, VIDIOC_QBUF, &buf) < 0)
            {
                Debug(QS("VIDIOC_QBUF failed for buffer %d: %s", i, strerror(errno)));
            }
        }

        // Start streaming
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(m_platform_data->fd, VIDIOC_STREAMON, &type) < 0)
        {
            Debug(QS("VIDIOC_STREAMON failed: %s", strerror(errno)));
            return QImage();
        }
        m_platform_data->streaming = true;
        Debug(QS("Streaming started"));
    }

    // Wait for and dequeue a buffer
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Use select() to wait for data with timeout
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_platform_data->fd, &fds);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int r = select(m_platform_data->fd + 1, &fds, 0, 0, &tv);
    if (r <= 0)
    {
        Debug(QS("select() timeout or error: %s", r == 0 ? "timeout" : strerror(errno)));
        return QImage();
    }

    if (ioctl(m_platform_data->fd, VIDIOC_DQBUF, &buf) < 0)
    {
        Debug(QS("VIDIOC_DQBUF failed: %s", strerror(errno)));
        return QImage();
    }

    // Convert frame data to QImage
    QImage frame;
    void *data = m_platform_data->buffers[buf.index].start;
    uint32_t size = buf.bytesused;

    if (m_platform_data->pixel_format == V4L2_PIX_FMT_MJPEG)
    {
        // MJPEG can be loaded directly
        frame.loadFromData((const uchar*)data, size, "JPEG");
    }
    else if (m_platform_data->pixel_format == V4L2_PIX_FMT_YUYV)
    {
        // Convert YUYV to RGB
        int w = m_platform_data->width;
        int h = m_platform_data->height;
        frame = QImage(w, h, QImage::Format_RGB888);

        const uchar *src = (const uchar*)data;
        for (int y = 0; y < h; y++)
        {
            uchar *dst = frame.scanLine(y);
            for (int x = 0; x < w; x += 2)
            {
                int i = (y * w + x) * 2;
                int y0 = src[i];
                int u  = src[i + 1];
                int y1 = src[i + 2];
                int v  = src[i + 3];

                // Convert YUV to RGB (simplified)
                int c0 = y0 - 16;
                int c1 = y1 - 16;
                int d = u - 128;
                int e = v - 128;

                auto clamp = [](int val) -> uchar {
                    return (uchar)(val < 0 ? 0 : (val > 255 ? 255 : val));
                };

                // First pixel
                dst[x * 3 + 0] = clamp((298 * c0 + 409 * e + 128) >> 8);
                dst[x * 3 + 1] = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                dst[x * 3 + 2] = clamp((298 * c0 + 516 * d + 128) >> 8);

                // Second pixel
                dst[(x + 1) * 3 + 0] = clamp((298 * c1 + 409 * e + 128) >> 8);
                dst[(x + 1) * 3 + 1] = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                dst[(x + 1) * 3 + 2] = clamp((298 * c1 + 516 * d + 128) >> 8);
            }
        }
    }
    else
    {
        Debug(QS("Unsupported pixel format: 0x%x", m_platform_data->pixel_format));
    }

    // Re-queue the buffer
    if (ioctl(m_platform_data->fd, VIDIOC_QBUF, &buf) < 0)
    {
        Debug(QS("VIDIOC_QBUF (re-queue) failed: %s", strerror(errno)));
    }

    return frame;

#elif defined(USE_GSTREAMER)
    //GStreamer frame capture
    if (!m_platform_data->pipeline || !m_platform_data->sink)
        return QImage();

    //Start pipeline if not running
    GstState state;
    gst_element_get_state(m_platform_data->pipeline, &state, 0, 0);
    if (state != GST_STATE_PLAYING)
    {
        gst_element_set_state(m_platform_data->pipeline, GST_STATE_PLAYING);
    }

    // Pull sample from appsink
    GstSample *sample = gst_app_sink_try_pull_sample(
        GST_APP_SINK(m_platform_data->sink), GST_SECOND);
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

    //Get caps to determine format
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *s = gst_caps_get_structure(caps, 0);
    int width, height;
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);

    QImage frame((uchar*)map.data, width, height,
                 QImage::Format_RGB888);
    QImage copy = frame.copy();

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return copy;

#else
    return QImage();
#endif
}

void
WebcamSource::startLivePreview()
{
    if (!m_is_initialized)
    {
        Debug(QS("startLivePreview: not initialized"));
        return;
    }

    if (m_live_preview_active)
        return;

    Debug(QS("Starting live preview"));
    m_live_preview_active = true;
    m_preview_timer->start();
}

void
WebcamSource::stopLivePreview()
{
    if (!m_live_preview_active)
        return;

    Debug(QS("Stopping live preview"));
    m_preview_timer->stop();
    m_live_preview_active = false;
}

bool
WebcamSource::isLivePreviewActive() const
{
    return m_live_preview_active;
}

QImage
WebcamSource::requestPreviewFrame()
{
    return captureFrame();
}

void
WebcamSource::onPreviewTimer()
{
    if (!m_live_preview_active)
        return;

    QImage frame = captureFrame();
    if (!frame.isNull())
    {
        emit previewFrameReady(frame);
    }
}

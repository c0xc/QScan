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

#include "scan/sanescandevice.hpp"
#include "core/classlogger.hpp"
#include <sane/sane.h>
#include <QImage>

//SANE constants that may not be available in all versions
#ifndef SANE_NAME_SCAN_RESOLUTION
#define SANE_NAME_SCAN_RESOLUTION "resolution"
#endif
#ifndef SANE_NAME_SCAN_MODE
#define SANE_NAME_SCAN_MODE "mode"
#endif
#ifndef SANE_NAME_SCAN_SOURCE
#define SANE_NAME_SCAN_SOURCE "source"
#endif
#ifndef SANE_NAME_DOCUMENT_FEEDER
#define SANE_NAME_DOCUMENT_FEEDER "Automatic Document Feeder"
#endif

//Global SANE reference counter to manage sane_init/sane_exit lifecycle
static int g_sane_ref_count = 0;
static bool g_sane_initialized = false;

SANEScanDevice::SANEScanDevice(const QString &device_name,
                               const QString &device_desc,
                               QObject *parent)
              : ScanSource(parent),
                m_device_name(device_name),
                m_device_desc(device_desc),
                m_handle(0),
                m_is_scanning(false),
                m_is_initialized(false)
{
    //Increment SANE reference count; sane_init() called in initialize()
    g_sane_ref_count++;
}

SANEScanDevice::~SANEScanDevice()
{
    //Close device handle if open
    if (m_handle)
    {
        sane_close(m_handle);
        m_handle = 0;
    }

    //Decrement SANE reference count and cleanup if last instance
    g_sane_ref_count--;
    if (g_sane_ref_count == 0 && g_sane_initialized)
    {
        Debug(QS("Last SANE device destroyed, calling sane_exit()"));
        sane_exit();
        g_sane_initialized = false;
    }
}

QList<ScanDeviceInfo>
SANEScanDevice::enumerateDevices()
{
    QList<ScanDeviceInfo> devices;

    //Initialize SANE library for device enumeration
    Debug(QS("Calling sane_init()"));
    SANE_Int version;
    SANE_Status status = sane_init(&version, 0);
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("sane_init() FAILED with status: %d (%s)",
                 status, sane_strstatus(status)));
        return devices;
    }
    Debug(QS("sane_init() succeeded, version: %d.%d.%d",
             SANE_VERSION_MAJOR(version), SANE_VERSION_MINOR(version), SANE_VERSION_BUILD(version)));
    g_sane_initialized = true;

    //Query SANE for available scanner devices
    const SANE_Device **device_list;
    Debug(QS("Calling sane_get_devices()"));
    status = sane_get_devices(&device_list, SANE_FALSE);
    if (status == SANE_STATUS_GOOD && device_list)
    {
        for (int i = 0; device_list[i]; ++i)
        {
            const SANE_Device *dev = device_list[i];
            QString name = QString::fromLatin1(dev->name);
            QString desc = QString::fromLatin1(dev->model);
            if (!desc.isEmpty() && dev->vendor && dev->vendor[0])
            {
                desc = QString::fromLatin1(dev->vendor) + " " + desc;
            }

            Debug(QS("Found SANE device [%d]: name=<%s>, vendor=<%s>, model=<%s>, type=<%s>",
                     i, dev->name, dev->vendor ? dev->vendor : "NULL",
                     dev->model ? dev->model : "NULL", dev->type ? dev->type : "NULL"));

            ScanDeviceInfo info(name, desc, ScanDeviceType::SCANNER);
            devices.append(info);
        }
        Debug(QS("Enumerated %d SANE device(s)", devices.count()));
    }
    else
    {
        Debug(QS("sane_get_devices() FAILED with status: %d (%s)",
                 status, sane_strstatus(status)));
    }

    //Note: sane_exit() not called here; managed by device instance destructors

    return devices;
}

ScanCapabilities
SANEScanDevice::capabilities() const
{
    return m_capabilities;
}

QString
SANEScanDevice::deviceName() const
{
    return m_device_name;
}

QString
SANEScanDevice::deviceDescription() const
{
    return m_device_desc;
}

bool
SANEScanDevice::initialize()
{
    if (m_is_initialized)
    {
        Debug(QS("Device <%s> already initialized", CSTR(m_device_name)));
        return true;
    }

    //Ensure SANE library is initialized
    if (!g_sane_initialized)
    {
        SANE_Int version;
        SANE_Status status = sane_init(&version, 0);
        if (status != SANE_STATUS_GOOD)
        {
            Debug(QS("sane_init() FAILED: %d (%s)", status, sane_strstatus(status)));
            return false;
        }
        g_sane_initialized = true;
        Debug(QS("SANE library initialized, version: %d.%d.%d",
                 SANE_VERSION_MAJOR(version), SANE_VERSION_MINOR(version), SANE_VERSION_BUILD(version)));
    }

    //Open SANE device handle; stored in m_handle until destructor
    Debug(QS("Opening device <%s>", CSTR(m_device_name)));
    SANE_Status status = sane_open(
        m_device_name.toLocal8Bit().constData(),
        &m_handle
    );
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("sane_open() FAILED for device <%s> with status: %d (%s)",
                 CSTR(m_device_name), status, sane_strstatus(status)));
        return false;
    }
    Debug(QS("sane_open() succeeded for device <%s>, handle: %p",
             CSTR(m_device_name), m_handle));

    queryCapabilities();
    m_is_initialized = true;
    Debug(QS("Device <%s> initialized successfully", CSTR(m_device_name)));
    return true;
}

bool
SANEScanDevice::startScan(const ScanParameters &params)
{
    if (!m_is_initialized || !m_handle)
        return false;
    
    m_is_scanning = true;
    emit scanStarted();
    
    //Start SANE scan operation
    SANE_Status status = sane_start(m_handle);
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("sane_start() FAILED: %d (%s)", status, sane_strstatus(status)));
        emit scanError(QString("Failed to start scan: %1").arg(sane_strstatus(status)));
        m_is_scanning = false;
        return false;
    }

    //Read image data from scanner
    QImage image = readImage();
    if (image.isNull())
    {
        Debug(QS("Failed to read image from scanner"));
        emit scanError("Failed to read image data from scanner");
        m_is_scanning = false;
        return false;
    }

    //Emit scanned page
    emit pageScanned(image, 0);
    emit scanComplete();
    
    m_is_scanning = false;
    return true;
}

void
SANEScanDevice::cancelScan()
{
    //Cancel active SANE scan operation
    if (m_handle && m_is_scanning)
    {
        sane_cancel(m_handle);
    }
    m_is_scanning = false;
}

bool
SANEScanDevice::isScanning() const
{
    return m_is_scanning;
}

bool
SANEScanDevice::isOpen() const
{
    return m_handle != 0;
}

QSizeF
SANEScanDevice::currentDocumentSize() const
{
    return m_current_document_size;
}

void
SANEScanDevice::queryCapabilities()
{
    //Query SANE device options to determine scanner capabilities
    if (!m_handle)
        return;

    //SANE constants for option names
#ifndef SANE_NAME_SCAN_RESOLUTION
#define SANE_NAME_SCAN_RESOLUTION "resolution"
#endif
#ifndef SANE_NAME_SCAN_MODE
#define SANE_NAME_SCAN_MODE "mode"
#endif

    //Reset capabilities
    m_capabilities = ScanCapabilities();
    m_capabilities.preview_mode = PreviewMode::SingleFrame;
    m_capabilities.supports_color_mode = true;
    m_capabilities.supports_scan_settings = true;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;

    //Query all SANE options (typically numbered 1-99)
    for (int i = 1; i < 100; ++i)
    {
        const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, i);
        if (!opt_desc)
            break;

        if (opt_desc->name)
        {
            //Check for resolution options
            if (strcmp(opt_desc->name, SANE_NAME_SCAN_RESOLUTION) == 0)
            {
                if (opt_desc->type == SANE_TYPE_INT && opt_desc->constraint_type == SANE_CONSTRAINT_RANGE)
                {
                    //Get resolution range
                    SANE_Int min_res = opt_desc->constraint.range->min;
                    SANE_Int max_res = opt_desc->constraint.range->max;
                    SANE_Int step = opt_desc->constraint.range->quant;

                    //Add common resolutions within range
                    if (min_res <= 75 && 75 <= max_res) m_capabilities.supported_resolutions << 75;
                    if (min_res <= 150 && 150 <= max_res) m_capabilities.supported_resolutions << 150;
                    if (min_res <= 300 && 300 <= max_res) m_capabilities.supported_resolutions << 300;
                    if (min_res <= 600 && 600 <= max_res) m_capabilities.supported_resolutions << 600;
                    if (min_res <= 1200 && 1200 <= max_res) m_capabilities.supported_resolutions << 1200;
                }
            }
            //Check for color mode options
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_MODE) == 0)
            {
                if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    m_capabilities.supported_color_modes.clear();
                    for (int j = 0; opt_desc->constraint.string_list[j] != 0; ++j)
                    {
                        const char *mode_str = opt_desc->constraint.string_list[j];
                        if (strcmp(mode_str, "Color") == 0 || strcmp(mode_str, "color") == 0)
                            m_capabilities.supported_color_modes << "Color";
                        else if (strcmp(mode_str, "Gray") == 0 || strcmp(mode_str, "gray") == 0)
                            m_capabilities.supported_color_modes << "Gray";
                        else if (strcmp(mode_str, "Lineart") == 0 || strcmp(mode_str, "Binary") == 0)
                            m_capabilities.supported_color_modes << "BW";
                    }
                }
            }
            //Check for source options (ADF support)
            else if (strcmp(opt_desc->name, "source") == 0)
            {
                if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    for (int j = 0; opt_desc->constraint.string_list[j] != 0; ++j)
                    {
                        const char *source_str = opt_desc->constraint.string_list[j];
                        if (strcmp(source_str, "Automatic Document Feeder") == 0 ||
                            strcmp(source_str, "ADF") == 0)
                        {
                            m_capabilities.supports_auto_feed = true;
                            m_capabilities.supports_multi_page = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    //Set reasonable defaults if no options found
    if (m_capabilities.supported_resolutions.isEmpty())
        m_capabilities.supported_resolutions << 75 << 150 << 300 << 600;
    if (m_capabilities.supported_color_modes.isEmpty())
        m_capabilities.supported_color_modes << "Color" << "Gray" << "BW";
}

bool
SANEScanDevice::scanPage()
{
    //Helper method for multi-page scanning (ADF)
    //TODO: Implement when ADF support is added
    return false;
}

QImage
SANEScanDevice::readImage()
{
    if (!m_handle)
        return QImage();

    //Get scan parameters to determine image dimensions and format
    SANE_Parameters parms;
    SANE_Status status = sane_get_parameters(m_handle, &parms);
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("sane_get_parameters() FAILED: %d (%s)", status, sane_strstatus(status)));
        return QImage();
    }

    Debug(QS("Scan parameters: format=%d, last_frame=%d, lines=%d, depth=%d, pixels_per_line=%d, bytes_per_line=%d",
             parms.format, parms.last_frame, parms.lines, parms.depth,
             parms.pixels_per_line, parms.bytes_per_line));

    //Query resolution from SANE to calculate physical document size
    int resolution = 300;  //Default fallback
    const SANE_Option_Descriptor *opt_desc;
    for (int i = 1; i < 100; ++i)  //SANE options typically numbered 1-99
    {
        opt_desc = sane_get_option_descriptor(m_handle, i);
        if (!opt_desc)
            break;
        
        if (opt_desc->name && strcmp(opt_desc->name, SANE_NAME_SCAN_RESOLUTION) == 0)
        {
            SANE_Int res_value;
            SANE_Status res_status = sane_control_option(
                m_handle, i, SANE_ACTION_GET_VALUE, &res_value, 0
            );
            if (res_status == SANE_STATUS_GOOD)
            {
                resolution = res_value;
                Debug(QS("Retrieved resolution from SANE: %d DPI", resolution));
            }
            break;
        }
    }

    //Calculate physical document size in millimeters
    double width_mm = (parms.pixels_per_line * 25.4) / resolution;
    double height_mm = (parms.lines * 25.4) / resolution;
    m_current_document_size = QSizeF(width_mm, height_mm);
    Debug(QS("Calculated document size: %.1f x %.1f mm", width_mm, height_mm));

    //Allocate buffer for image data
    int total_bytes = parms.bytes_per_line * parms.lines;
    QByteArray buffer(total_bytes, 0);
    int bytes_read = 0;

    //Read image data from scanner in chunks
    while (bytes_read < total_bytes)
    {
        SANE_Int len = 0;
        status = sane_read(
            m_handle,
            (SANE_Byte*)(buffer.data() + bytes_read),
            total_bytes - bytes_read,
            &len
        );

        if (status == SANE_STATUS_EOF)
        {
            Debug(QS("sane_read() reached EOF after %d bytes", bytes_read));
            break;
        }

        if (status != SANE_STATUS_GOOD)
        {
            Debug(QS("sane_read() FAILED: %d (%s)", status, sane_strstatus(status)));
            return QImage();
        }

        bytes_read += len;
    }

    Debug(QS("Read %d bytes from scanner", bytes_read));

    //Convert SANE image data to QImage
    QImage image;
    if (parms.format == SANE_FRAME_GRAY)
    {
        //Grayscale image
        image = QImage(parms.pixels_per_line, parms.lines, QImage::Format_Grayscale8);
        memcpy(image.bits(), buffer.data(), bytes_read);
    }
    else if (parms.format == SANE_FRAME_RGB)
    {
        //RGB color image
        image = QImage(parms.pixels_per_line, parms.lines, QImage::Format_RGB888);
        memcpy(image.bits(), buffer.data(), bytes_read);
    }
    else
    {
        Debug(QS("Unsupported SANE frame format: %d", parms.format));
        return QImage();
    }

    return image;
}

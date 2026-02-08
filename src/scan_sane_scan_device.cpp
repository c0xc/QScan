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
}

SANEScanDevice::~SANEScanDevice()
{
    if (m_handle)
    {
        sane_close((SANE_Handle)m_handle);
    }
}

QList<ScanDeviceInfo>
SANEScanDevice::enumerateDevices()
{
    QList<ScanDeviceInfo> devices;

    // Initialize SANE
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

    // Get device list
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

    // Note: We don't call sane_exit() here because ScanManager might create devices
    // SANE will be cleaned up when the last device is destroyed

    return devices;
}

ScanCapabilities
SANEScanDevice::capabilities() const
{
    return m_capabilities;
}

QStringList
SANEScanDevice::supportedFormats() const
{
    return QStringList() << "JPG" << "PNG" << "PDF";
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

    // Open device
    Debug(QS("Opening device <%s>", CSTR(m_device_name)));
    SANE_Status status = sane_open(m_device_name.toLocal8Bit().constData(),
                                   (SANE_Handle*)&m_handle);
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
    if (!m_is_initialized)
        return false;
    
    m_is_scanning = true;
    emit scanStarted();
    
    // TODO: Set scan parameters (resolution, color mode, scan area)
    // TODO: Call sane_start()
    // TODO: Read image data with sane_read()
    // TODO: Convert to QImage
    
    // For now, emit error
    emit scanError("SANE scanning not yet fully implemented");
    m_is_scanning = false;
    return false;
}

void
SANEScanDevice::cancelScan()
{
    if (m_handle && m_is_scanning)
    {
        sane_cancel((SANE_Handle)m_handle);
    }
    m_is_scanning = false;
}

bool
SANEScanDevice::isScanning() const
{
    return m_is_scanning;
}

void
SANEScanDevice::queryCapabilities()
{
    // TODO: Query SANE options to determine capabilities
    // For now, set reasonable defaults
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;
    m_capabilities.supports_color_mode = true;
    m_capabilities.supported_resolutions << 75 << 150 << 300 << 600;
    m_capabilities.supported_color_modes << "Color" << "Gray" << "BW";
}

bool
SANEScanDevice::scanPage()
{
    // TODO: Implement SANE scanning
    return false;
}

QImage
SANEScanDevice::readImage()
{
    // TODO: Implement SANE image reading
    return QImage();
}

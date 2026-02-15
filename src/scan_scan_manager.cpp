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

#include "scan/scan_manager.hpp"
#include "scan/scanner_source.hpp"
#include "scan/webcam_source.hpp"
#include "scan/mobile_source.hpp"
#include "core/classlogger.hpp"

ScanManager::ScanManager(QObject *parent)
           : QObject(parent)
{
}

ScanManager::~ScanManager()
{
}

bool
ScanManager::initialize()
{
    Debug(QS("Initializing, clearing device list"));
    m_devices.clear();

    // Enumerate scanner devices
    Debug(QS("Enumerating scanner devices..."));
    enumerateScanners();

    // Enumerate camera devices
    Debug(QS("Enumerating camera devices..."));
    enumerateCameras();

    Debug(QS("Initialization complete, total %lld device(s) found", static_cast<long long>(m_devices.count())));
    return true;  // Always succeed - even if no devices found
}

QList<ScanDeviceInfo>
ScanManager::availableDevices() const
{
    return m_devices;
}

ScanSource*
ScanManager::createScanSource(const QString &device_name, QObject *parent)
{
    Debug(QS("Creating scan source for device <%s>", CSTR(device_name)));

    // Find device info
    ScanDeviceInfo info;
    bool found = false;
    foreach (const ScanDeviceInfo &dev, m_devices)
    {
        if (dev.name == device_name)
        {
            info = dev;
            found = true;
            break;
        }
    }

    if (!found)
    {
        Debug(QS("Device <%s> NOT FOUND in enumerated devices list", CSTR(device_name)));
        return 0;
    }

    Debug(QS("Found device <%s>, type=<%s>, description=<%s>",
             CSTR(info.name), CSTR(info.typeString()), CSTR(info.description)));

    // Create appropriate source type
    switch (info.type)
    {
        case ScanDeviceType::SCANNER:
            Debug(QS("Creating scanner device for <%s>", CSTR(info.name)));
            return new ScannerSource(info.name, info.description, parent);

        case ScanDeviceType::CAMERA:
            Debug(QS("Creating camera device for <%s>", CSTR(info.name)));
            return new WebcamSource(info.name, info.description, parent);

        case ScanDeviceType::MOBILE:
            Debug(QS("Creating mobile device for <%s>", CSTR(info.name)));
            return new MobileSource(info.name, info.description, parent);

        default:
            Debug(QS("Unknown device type %d for device <%s>", (int)info.type, CSTR(info.name)));
            return 0;
    }
}

void
ScanManager::enumerateScanners()
{
    // Call static enumeration method from scanner implementation
    QList<ScanDeviceInfo> scanner_devices = ScannerSource::enumerateDevices();
    Debug(QS("Scanner enumeration returned %lld device(s)", static_cast<long long>(scanner_devices.count())));
    m_devices.append(scanner_devices);
}

void
ScanManager::enumerateCameras()
{
    // Call static enumeration method from camera implementation
    QList<ScanDeviceInfo> camera_devices = WebcamSource::enumerateDevices();
    Debug(QS("Camera enumeration returned %lld device(s)", static_cast<long long>(camera_devices.count())));
    m_devices.append(camera_devices);
    
    // Also enumerate mobile devices
    QList<ScanDeviceInfo> mobile_devices = MobileSource::enumerateDevices();
    Debug(QS("Mobile enumeration returned %lld device(s)", static_cast<long long>(mobile_devices.count())));
    m_devices.append(mobile_devices);
}

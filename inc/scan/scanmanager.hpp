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

#ifndef SCAN_SCANMANAGER_HPP
#define SCAN_SCANMANAGER_HPP

#include <QObject>
#include <QList>

#include "scan/scansource.hpp"

/**
 * Device type enumeration.
 */
enum class ScanDeviceType
{
    UNKNOWN,
    SCANNER,     // Hardware scanner device
    CAMERA,      // Camera/webcam device
    MOBILE       // Mobile device camera
};

/**
 * Device information for scanner selection.
 */
struct ScanDeviceInfo
{
    QString name;                // Device identifier
    QString description;         // User-friendly description
    ScanDeviceType type;         // Device type

    ScanDeviceInfo()
        : type(ScanDeviceType::UNKNOWN)
    {
    }

    ScanDeviceInfo(const QString &n, const QString &d, ScanDeviceType t)
        : name(n), description(d), type(t)
    {
    }

    // Helper to get display string for type
    QString typeString() const
    {
        switch (type)
        {
            case ScanDeviceType::SCANNER: return QObject::tr("Scanner");
            case ScanDeviceType::CAMERA:  return QObject::tr("Camera");
            default:                      return QObject::tr("Unknown");
        }
    }
};

/**
 * Manages scan source enumeration and creation.
 * Factory pattern for creating appropriate ScanSource instances.
 */
class ScanManager : public QObject
{
    Q_OBJECT

public:

    explicit
    ScanManager(QObject *parent = nullptr);

    ~ScanManager() override;

    /**
     * Initialize and enumerate all available scan sources.
     * Returns true on success.
     */
    bool
    initialize();

    /**
     * Returns list of all available scan devices.
     */
    QList<ScanDeviceInfo>
    availableDevices() const;

    /**
     * Create a ScanSource for the specified device.
     * Caller takes ownership of the returned pointer.
     * Returns nullptr if device not found or creation failed.
     */
    ScanSource*
    createScanSource(const QString &device_name, QObject *parent = nullptr);

private:

    QList<ScanDeviceInfo> m_devices;

    /**
     * Enumerate hardware scanner devices.
     */
    void
    enumerateScanners();

    /**
     * Enumerate camera devices.
     */
    void
    enumerateCameras();

};

#endif // SCAN_SCANMANAGER_HPP

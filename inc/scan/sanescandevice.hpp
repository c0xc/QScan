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

#ifndef SCAN_SANESCANDEVICE_HPP
#define SCAN_SANESCANDEVICE_HPP

#include "scan/scansource.hpp"
#include "scan/scanmanager.hpp"

// Forward declare SANE types to avoid exposing SANE headers
typedef void* SANE_Handle;

/**
 * SANE scanner implementation.
 * All SANE-specific code is isolated in this class.
 * GUI layer never directly calls SANE functions.
 */
class SANEScanDevice : public ScanSource
{
    Q_OBJECT

public:

    /**
     * Static method to enumerate available SANE scanner devices.
     * Called by ScanManager during initialization.
     * Returns empty list if SANE is not available or initialization fails.
     */
    static QList<ScanDeviceInfo>
    enumerateDevices();

    /**
     * Constructor.
     * @param device_name SANE device name (e.g., "hpaio:/usb/...")
     * @param device_desc User-friendly device description
     */
    SANEScanDevice(const QString &device_name,
                   const QString &device_desc,
                   QObject *parent = nullptr);

    ~SANEScanDevice() override;

    // ScanSource interface implementation
    ScanCapabilities
    capabilities() const override;

    QStringList
    supportedFormats() const override;

    QString
    deviceName() const override;

    QString
    deviceDescription() const override;

    bool
    initialize() override;

    bool
    startScan(const ScanParameters &params) override;

    void
    cancelScan() override;

    bool
    isScanning() const override;

private:

    QString m_device_name;
    QString m_device_desc;
    ScanCapabilities m_capabilities;
    SANE_Handle *m_handle;
    bool m_is_scanning;
    bool m_is_initialized;

    /**
     * Query device capabilities from SANE.
     */
    void
    queryCapabilities();

    /**
     * Scan a single page or start ADF batch.
     */
    bool
    scanPage();

    /**
     * Read image data from SANE.
     */
    QImage
    readImage();

};

#endif // SCAN_SANESCANDEVICE_HPP

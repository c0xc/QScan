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

#ifndef SCAN_MOBILESOURCE_HPP
#define SCAN_MOBILESOURCE_HPP

#include "scan/scansource.hpp"
#include "scan/scanmanager.hpp"

/**
 * Mobile device capture implementation (stub).
 * Placeholder for mobile camera integration.
 */
class MobileSource : public ScanSource
{
    Q_OBJECT

public:

    /**
     * Static method to enumerate available mobile devices.
     * Called by ScanManager during initialization.
     */
    static QList<ScanDeviceInfo>
    enumerateDevices();

    /**
     * Constructor.
     */
    MobileSource(const QString &device_identifier,
                 const QString &device_description,
                 QObject *parent = 0);

    ~MobileSource() override;

    // ScanSource interface implementation
    ScanCapabilities
    capabilities() const override;

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

    bool
    isOpen() const override;

    bool
    startPreview() override;

    void
    stopPreview() override;

    bool
    isPreviewActive() const override;

private:

    QString m_device_identifier;
    QString m_device_description;
    ScanCapabilities m_capabilities;
    bool m_is_scanning;
    bool m_is_initialized;
    bool m_live_preview_active;

};

#endif // SCAN_MOBILESOURCE_HPP
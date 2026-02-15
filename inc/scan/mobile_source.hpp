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

#ifndef SCAN_MOBILE_SOURCE_HPP
#define SCAN_MOBILE_SOURCE_HPP

#include <QTimer>

#include "scan/scan_source.hpp"
#include "scan/scan_device_info.hpp"

class MobileSource : public ScanSource
{
    Q_OBJECT

public:

    static QList<ScanDeviceInfo>
    enumerateDevices();

    MobileSource(const QString &device_identifier,
                 const QString &device_description,
                 QObject *parent = 0);

    ~MobileSource() override;

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

private slots:

    void
    onPreviewTimer();

private:

    QString m_device_identifier;
    QString m_device_description;
    ScanCapabilities m_capabilities;
    bool m_is_scanning;
    bool m_is_initialized;
    bool m_live_preview_active;

    //Preview timer stub,will be replaced by http/websocket stream
    QTimer *m_preview_timer;

    //Stub endpoint details,used by future python backend integration
    QString m_backend_url;
    QString m_session_id;

};

#endif // SCAN_MOBILE_SOURCE_HPP

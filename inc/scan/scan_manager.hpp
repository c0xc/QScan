/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (philip@c0xc.net)
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

#ifndef SCAN_SCAN_MANAGER_HPP
#define SCAN_SCAN_MANAGER_HPP

#include <QObject>
#include <QList>

#include "scan/scan_device_info.hpp"
#include "scan/scan_source.hpp"

class ScanManager : public QObject
{
    Q_OBJECT

public:

    explicit
    ScanManager(QObject *parent = nullptr);

    ~ScanManager() override;

    bool
    initialize();

    QList<ScanDeviceInfo>
    availableDevices() const;

    ScanSource*
    createScanSource(const QString &device_name, QObject *parent = nullptr);

    //Backend-implemented endpoint probe for manual source add flows
    //GUI should not perform protocol-specific network requests directly
    bool
    testEsclEndpoint(const QString &user_input,
                     QString &normalized_url_out,
                     QString &error_out,
                     QString &suggested_label_out);

private:

    QList<ScanDeviceInfo> m_devices;

    void
    enumerateScanners();

    void
    enumerateCameras();

};

#endif //SCAN_SCAN_MANAGER_HPP

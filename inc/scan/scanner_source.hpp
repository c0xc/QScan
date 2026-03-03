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

#ifndef SCAN_SCANNER_SOURCE_HPP
#define SCAN_SCANNER_SOURCE_HPP

#include <memory>
#include <QThread>
#include <QPointer>
#include <atomic>

#include "scan/scan_source.hpp"
#include "scan/scan_device_info.hpp"

class ScannerBackend;

//Image source for flatbed/ADF scanners
class ScannerSource : public ScanSource
{
    Q_OBJECT

public:

    static QList<ScanDeviceInfo>
    enumerateDevices();

    static bool
    enumerateDevices(QList<ScanDeviceInfo> &devices);

    ScannerSource(const QString &device_name,
                 const QString &device_desc,
                 QObject *parent = 0);

    ~ScannerSource() override;

    ScanCapabilities
    capabilities() const override;

    //Scanner-only queries (webcam not affected)
    QStringList
    supportedInputSources() const;

    bool
    supportsDuplex() const;

    QStringList
    supportedScanSides() const;

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

    QSizeF
    currentDocumentSize() const override;

    ReportedDocumentSize
    reportedDocumentSize() const override;

private:

    QString m_device_name;
    QString m_device_desc;
    bool m_is_scanning;
    bool m_is_initialized;

    bool m_last_auto_page_size;

    bool m_preview_active;
    QPointer<QThread> m_preview_thread;

    std::shared_ptr<std::atomic<bool>> m_preview_cancel_requested;

    std::shared_ptr<std::atomic<bool>> m_scan_cancel_requested;
    QPointer<QThread> m_scan_thread;

    //Backend instance (owns backend-private state)
    std::shared_ptr<ScannerBackend> m_backend;

};

#endif //SCAN_SCANNER_SOURCE_HPP

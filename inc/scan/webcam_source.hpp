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

#ifndef SCAN_WEBCAM_SOURCE_HPP
#define SCAN_WEBCAM_SOURCE_HPP

#include <QTimer>
#include <memory>

#include "scan/scan_source.hpp"
#include "scan/scan_device_info.hpp"

class WebcamBackend;

class WebcamSource : public ScanSource
{
    Q_OBJECT

public:

    static QList<ScanDeviceInfo>
    enumerateDevices();

    WebcamSource(const QString &device_identifier,
                 const QString &device_description,
                 QObject *parent = 0);

    ~WebcamSource() override;

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
    startPreview() override;

    void
    stopPreview() override;

    bool
    isPreviewActive() const override;

    bool
    isOpen() const override;

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
    QTimer *m_preview_timer;
    int m_frame_fail_count;

    //Backend instance (owns backend-private state)
    std::unique_ptr<WebcamBackend> m_backend;

    void
    queryCapabilities();

    QImage
    captureFrame();

};

#endif //SCAN_WEBCAM_SOURCE_HPP

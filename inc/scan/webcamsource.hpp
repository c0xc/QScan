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

#ifndef SCAN_WEBCAMSOURCE_HPP
#define SCAN_WEBCAMSOURCE_HPP

#include <QTimer>
#include "scan/scansource.hpp"
#include "scan/scanmanager.hpp"

/**
 * Camera/webcam capture implementation.
 * Platform-agnostic interface - implementation varies by platform and build configuration.
 * Implementation details are hidden in the source file.
 */
class WebcamSource : public ScanSource
{
    Q_OBJECT

public:

    /**
     * Static method to enumerate available camera devices.
     * Called by ScanManager during initialization.
     * Platform-specific implementation.
     */
    static QList<ScanDeviceInfo>
    enumerateDevices();

    /**
     * Constructor.
     * @param device_identifier Platform-specific device identifier
     * @param device_description User-friendly device description
     */
    WebcamSource(const QString &device_identifier,
                 const QString &device_description,
                 QObject *parent = nullptr);

    ~WebcamSource() override;

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

    void
    startLivePreview() override;

    void
    stopLivePreview() override;

    bool
    isLivePreviewActive() const override;

    QImage
    requestPreviewFrame() override;

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

    // Platform-specific implementation data (opaque pointer)
    struct PlatformData;
    PlatformData *m_platform_data;

    /**
     * Query camera capabilities.
     */
    void
    queryCapabilities();

    /**
     * Capture a single frame from camera.
     */
    QImage
    captureFrame();

};

#endif // SCAN_WEBCAMSOURCE_HPP

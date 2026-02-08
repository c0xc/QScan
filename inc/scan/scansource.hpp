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

#ifndef SCAN_SCANSOURCE_HPP
#define SCAN_SCANSOURCE_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>

#include "scan/scancapabilities.hpp"

/**
 * Abstract interface for all scan sources (SANE scanners, webcams, etc.).
 * This provides a unified interface for the GUI layer.
 * Implementations must be in separate files to isolate backend-specific code.
 */
class ScanSource : public QObject
{
    Q_OBJECT

public:

    explicit
    ScanSource(QObject *parent = 0) : QObject(parent) {}

    virtual
    ~ScanSource() = default;

    /**
     * Returns the capabilities of this scan source.
     * GUI uses this to show/hide features dynamically.
     */
    virtual ScanCapabilities
    capabilities() const = 0;

    /**
     * Returns supported output formats (e.g., "JPG", "PNG", "PDF").
     */
    virtual QStringList
    supportedFormats() const = 0;

    /**
     * Returns the device name/identifier.
     */
    virtual QString
    deviceName() const = 0;

    /**
     * Returns a user-friendly device description.
     */
    virtual QString
    deviceDescription() const = 0;

    /**
     * Initialize the device. Must be called before scanning.
     * Returns true on success.
     */
    virtual bool
    initialize() = 0;

    /**
     * Start a scan operation with the given parameters.
     * For multi-page scanners, this may emit pageScanned() multiple times.
     * Returns true if scan started successfully.
     */
    virtual bool
    startScan(const ScanParameters &params) = 0;

    /**
     * Cancel an in-progress scan operation.
     */
    virtual void
    cancelScan() = 0;

    /**
     * Check if a scan is currently in progress.
     */
    virtual bool
    isScanning() const = 0;

    /**
     * Start live preview stream.
     * Only meaningful for sources with PreviewMode::LiveStream.
     * The source will emit previewFrameReady() periodically.
     * Default implementation does nothing.
     */
    virtual void
    startLivePreview() {}

    /**
     * Stop live preview stream.
     * Default implementation does nothing.
     */
    virtual void
    stopLivePreview() {}

    /**
     * Check if live preview is currently active.
     */
    virtual bool
    isLivePreviewActive() const { return false; }

    /**
     * Request a single preview frame synchronously.
     * For scanners: triggers a low-res preview scan.
     * For webcams: captures current frame from stream.
     * Returns empty image if not supported or on error.
     */
    virtual QImage
    requestPreviewFrame() { return QImage(); }

signals:

    /**
     * Emitted when scanning starts.
     */
    void
    scanStarted();

    /**
     * Emitted when a page has been scanned.
     * For single-page sources, page_number is always 0.
     * For multi-page sources (ADF), page_number increments.
     */
    void
    pageScanned(const QImage &image, int page_number);

    /**
     * Emitted when all pages have been scanned.
     */
    void
    scanComplete();

    /**
     * Emitted if an error occurs during scanning.
     */
    void
    scanError(const QString &error);

    /**
     * Emitted to update scan progress (0-100%).
     */
    void
    progressChanged(int percent);

    /**
     * Emitted when a preview frame is ready (for async preview operations).
     */
    void
    previewFrameReady(const QImage &image);

};

#endif // SCAN_SCANSOURCE_HPP

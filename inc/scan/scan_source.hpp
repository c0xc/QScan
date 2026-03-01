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

#ifndef SCAN_SCAN_SOURCE_HPP
#define SCAN_SCAN_SOURCE_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include <QSizeF>

#include "scan/scan_capabilities.hpp"
#include "scan/scan_page_info.hpp"

class ScanSource : public QObject
{
    Q_OBJECT

public:

    explicit
    ScanSource(QObject *parent = 0) : QObject(parent) {}

    virtual
    ~ScanSource() = default;

    virtual ScanCapabilities
    capabilities() const = 0;

    virtual QString
    deviceName() const = 0;

    virtual QString
    deviceDescription() const = 0;

    virtual bool
    initialize() = 0;

    virtual bool
    startScan(const ScanParameters &params) = 0;

    virtual void
    cancelScan() = 0;

    virtual bool
    isScanning() const = 0;

    virtual bool
    isOpen() const = 0;

    virtual bool
    startPreview() { return false; }

    virtual void
    stopPreview() {}

    virtual bool
    isPreviewActive() const { return false; }

    struct ReportedDocumentSize
    {
        bool valid = false;
        QSizeF mm_size;
        QString paper_name;
    };

    //Returns physical paper size only when the backend/device truly reports it
    //No estimates/guesses should be surfaced here
    virtual ReportedDocumentSize
    reportedDocumentSize() const { return ReportedDocumentSize(); }

    virtual QSizeF
    currentDocumentSize() const { return QSizeF(); }

    virtual bool
    documentSizeIsReported() const { return reportedDocumentSize().valid; }

signals:

    void
    scanStarted();

    void
    pageScanned(const QImage &image, int page_number, const qscan::ScanPageInfo &page_info);

    void
    scanComplete();

    void
    scanCanceled();

    void
    scanError(const QString &error);

    void
    scanCancelRequested();

    void
    scanStatusMessage(const QString &message);

    void
    progressChanged(int percent);

    void
    previewFrameReady(const QImage &image);

};

#endif //SCAN_SCAN_SOURCE_HPP

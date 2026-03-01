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

#ifndef SCAN_SCANNER_BACKEND_HPP
#define SCAN_SCANNER_BACKEND_HPP

#include <functional>
#include <QImage>
#include <QString>
#include <QSizeF>

#include "scan/scan_capabilities.hpp"
#include "scan/scan_page_info.hpp"

//Backend interface for local flatbed/ADF scanner implementations
class ScannerBackend
{
public:

    using PageCallback = std::function<bool(const QImage &image, int page_number, const qscan::ScanPageInfo &page_info)>;

    virtual
    ~ScannerBackend() = default;

    virtual bool
    initialize(const QString &device_name) = 0;

    virtual void
    cancelScan() = 0;

    virtual bool
    isOpen() const = 0;

    virtual ScanCapabilities
    capabilities() const = 0;

    virtual QSizeF
    currentDocumentSize() const = 0;

    //If true, the returned currentDocumentSize() comes from the device/backend as a physical paper size
    //(e.g. an ADF scanner that detects the paper dimensions). If false but currentDocumentSize() is non-empty,
    //it should be treated as an estimate derived from pixels and DPI
    virtual bool
    documentSizeIsReported() const { return false; }

    virtual bool
    scan(const ScanParameters &params, const PageCallback &on_page, QString &error_out) = 0;

};

#endif //SCAN_SCANNER_BACKEND_HPP

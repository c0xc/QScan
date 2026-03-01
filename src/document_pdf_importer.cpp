/****************************************************************************
**
** Copyright (C) 2026 Philip Seeger
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

#include <QtGlobal>

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QSizeF>

#include "document/pdf_importer.hpp"

#if defined(QSCAN_HAVE_QTPDF)
#include <QPdfDocument>
#endif

static QSize
pixelSizeFromPointsAtDpi(const QSizeF &points, int dpi)
{
    //QtPdf page sizes are in points (1/72 inch)
    //px = points * dpi / 72
    const double scale = static_cast<double>(dpi) / 72.0;
    const int w = qMax(1, qRound(points.width() * scale));
    const int h = qMax(1, qRound(points.height() * scale));
    return QSize(w, h);
}

static QSizeF
mmSizeFromPoints(const QSizeF &points)
{
    //1 point = 1/72 inch
    //mm = points / 72 * 25.4
    const double mm_per_point = 25.4 / 72.0;
    return QSizeF(points.width() * mm_per_point, points.height() * mm_per_point);
}

bool
PdfImporter::importPdf(const QString &path, int dpi, QVector<ScannedPage> &out_pages, QString &error_out)
{
    out_pages.clear();
    error_out.clear();

    if (dpi <= 0)
    {
        error_out = QStringLiteral("Invalid DPI");
        return false;
    }

#if !defined(QSCAN_HAVE_QTPDF)
    Q_UNUSED(path);
    error_out = QStringLiteral("PDF import is not available in this build");
    return false;
#else
    QPdfDocument pdf;
    const QPdfDocument::Status status = pdf.load(path);
    if (status != QPdfDocument::Status::Ready)
    {
        error_out = QStringLiteral("Failed to open PDF");
        return false;
    }

    const int page_count = pdf.pageCount();
    if (page_count <= 0)
    {
        error_out = QStringLiteral("PDF has no pages");
        return false;
    }

    const QFileInfo fi(path);

    for (int page_index = 0; page_index < page_count; ++page_index)
    {
        const QSizeF points = pdf.pagePointSize(page_index);
        const QSize px = pixelSizeFromPointsAtDpi(points, dpi);

        QImage image = pdf.render(page_index, px);
        if (image.isNull())
        {
            error_out = QStringLiteral("Failed to render PDF page %1").arg(page_index + 1);
            out_pages.clear();
            return false;
        }

        ScannedPage page(image);
        page.setScanTime(QDateTime::currentDateTime());
        page.setAcquisitionKind(ScannedPage::AcquisitionKind::Scan);
        page.setScanResolutionDpi(dpi);
        page.setEffectiveResolutionDpi(dpi);
        page.setSourceName(fi.fileName());
        page.setBackendKind(QStringLiteral("PDF"));

        const QSizeF mm = mmSizeFromPoints(points);
        page.setScanAreaMm(mm);

        out_pages.push_back(page);
    }

    return true;
#endif
}

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

#ifndef DOCUMENT_DOCUMENTEXPORTER_HPP
#define DOCUMENT_DOCUMENTEXPORTER_HPP

#include <QString>

#include "document/document.hpp"
#include "document/scannedpage.hpp"

/**
 * Handles exporting scanned documents to various formats.
 * Abstracts the export mechanism to allow swapping backends.
 */
class DocumentExporter
{
public:

    /**
     * Supported export formats.
     */
    enum ExportFormat
    {
        JPG,
        PNG,
        PDF
    };

    DocumentExporter();

    /**
     * Export a multi-page document.
     * For PDF: creates multi-page PDF
     * For JPG/PNG: creates multiple files with page numbers
     */
    bool
    exportDocument(const Document &doc,
                   const QString &path,
                   ExportFormat format);

    /**
     * Export a single image.
     */
    bool
    exportSingleImage(const ScannedPage &page,
                      const QString &path,
                      ExportFormat format);

    /**
     * Get last error message.
     */
    QString
    lastError() const;

private:

    QString m_last_error;

    /**
     * Export document to PDF using QPdfWriter.
     */
    bool
    exportToPDF(const Document &doc, const QString &path);

    /**
     * Export image with metadata.
     */
    bool
    exportImage(const QImage &image,
                const QString &path,
                const QString &format,
                const QString &scanner_name,
                const QDateTime &scan_time);

};

#endif // DOCUMENT_DOCUMENTEXPORTER_HPP

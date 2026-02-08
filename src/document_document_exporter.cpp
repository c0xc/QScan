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

#include "document/documentexporter.hpp"

DocumentExporter::DocumentExporter()
{
}

bool
DocumentExporter::exportDocument(const Document &doc,
                                const QString &path,
                                ExportFormat format)
{
    // TODO: Implement document export
    m_last_error = "Export not yet implemented";
    return false;
}

bool
DocumentExporter::exportSingleImage(const ScannedPage &page,
                                   const QString &path,
                                   ExportFormat format)
{
    // TODO: Implement single image export
    m_last_error = "Export not yet implemented";
    return false;
}

QString
DocumentExporter::lastError() const
{
    return m_last_error;
}

bool
DocumentExporter::exportToPDF(const Document &doc, const QString &path)
{
    // TODO: Implement PDF export using QPdfWriter
    m_last_error = "PDF export not yet implemented";
    return false;
}

bool
DocumentExporter::exportImage(const QImage &image,
                             const QString &path,
                             const QString &format,
                             const QString &scanner_name,
                             const QDateTime &scan_time)
{
    // TODO: Implement image export with metadata
    m_last_error = "Image export not yet implemented";
    return false;
}

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

#ifndef DOCUMENT_PDF_IMPORTER_HPP
#define DOCUMENT_PDF_IMPORTER_HPP

#include <QString>
#include <QVector>

#include "document/scannedpage.hpp"

class PdfImporter
{
public:

    static bool
    importPdf(const QString &path, int dpi, QVector<ScannedPage> &out_pages, QString &error_out);

};

#endif //DOCUMENT_PDF_IMPORTER_HPP

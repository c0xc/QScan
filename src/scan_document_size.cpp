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

#include "scan/document_size.hpp"

#include <QtMath>

namespace
{

struct PaperSize
{
    const char *name;
    qreal w_mm;
    qreal h_mm;
};

static bool
matchesPaperSize(const QSizeF &mm, qreal w, qreal h, qreal tol)
{
    if (mm.isEmpty())
        return false;

    const qreal a = mm.width();
    const qreal b = mm.height();

    const bool direct = (qAbs(a - w) <= tol) && (qAbs(b - h) <= tol);
    const bool swapped = (qAbs(a - h) <= tol) && (qAbs(b - w) <= tol);
    return direct || swapped;
}

} //namespace

QString
documentSizePaperNameForMm(const QSizeF &mm_size, qreal tolerance_mm)
{
    static const PaperSize sizes[] =
    {
        {"A0", 841.0, 1189.0},
        {"A1", 594.0, 841.0},
        {"A2", 420.0, 594.0},
        {"A3", 297.0, 420.0},
        {"A4", 210.0, 297.0},
        {"A5", 148.0, 210.0},
        {"A6", 105.0, 148.0},
        {"Letter", 216.0, 279.0},
        {"Legal", 216.0, 356.0},
    };

    for (const auto &s : sizes)
    {
        if (matchesPaperSize(mm_size, s.w_mm, s.h_mm, tolerance_mm))
            return QString::fromLatin1(s.name);
    }
    return QString();
}

QString
documentSizeFormatMm(const QSizeF &mm_size)
{
    if (mm_size.isEmpty())
        return QString();

    return QString("%1 x %2 mm")
        .arg(mm_size.width(), 0, 'f', 1)
        .arg(mm_size.height(), 0, 'f', 1);
}


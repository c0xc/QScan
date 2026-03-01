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

#include <QTransform>

#include "processing/rotate_processor.hpp"

RotateProcessor::RotateProcessor()
{
}

QImage
RotateProcessor::rotate(const QImage &input, int degrees)
{
    //TODO: Implement rotation for 90° increments
    //For now, use QTransform
    QTransform transform;
    transform.rotate(degrees);
    return input.transformed(transform);
}

QImage
RotateProcessor::rotate(const QImage &input, double degrees, QColor fill)
{
    //TODO: Implement arbitrary angle rotation with background fill
    //For now, use QTransform
    QTransform transform;
    transform.rotate(degrees);
    return input.transformed(transform, Qt::SmoothTransformation);
}

QImage
RotateProcessor::process(const QImage &input)
{
    //No-op: return copy with no rotation
    return input.copy();
}

QString
RotateProcessor::name() const
{
    return "Rotate";
}

bool
RotateProcessor::isAvailable() const
{
    return true;  //always available (Qt-only)
}

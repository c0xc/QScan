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

#include "processing/crop_processor.hpp"

CropProcessor::CropProcessor()
{
}

QImage
CropProcessor::crop(const QImage &input, const QRect &rect)
{
    // TODO: Implement cropping
    // For now, return a copy of the input
    return input.copy(rect);
}

QImage
CropProcessor::cropToQuad(const QImage &input, const QPolygonF &quad)
{
    // TODO: Implement quad cropping
    // Without OpenCV: simple bounding box crop
    // With OpenCV: delegate to SmartCaptureProcessor for perspective warp
    QRectF bounds = quad.boundingRect();
    return input.copy(bounds.toRect());
}

QImage
CropProcessor::process(const QImage &input)
{
    // No-op: return copy of entire image
    return input.copy();
}

QString
CropProcessor::name() const
{
    return "Crop";
}

bool
CropProcessor::isAvailable() const
{
    return true;  // Always available (Qt-only)
}

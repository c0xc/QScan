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

#ifndef PROCESSING_CROP_PROCESSOR_HPP
#define PROCESSING_CROP_PROCESSOR_HPP

#include <QRect>
#include <QPolygonF>

#include "processing/image_processor.hpp"

/**
 * Crop processor - crops images to specified regions.
 * Always available (Qt-only, no external dependencies).
 * 
 * Handles both manual cropping (user-drawn rectangle) and
 * automatic cropping (rectangle from BorderDetector).
 */
class CropProcessor : public ImageProcessor
{
public:

    CropProcessor();

    /**
     * Crop image to the given rectangle.
     * Used for both manual crops (user draws rect) and auto-crops (BorderDetector provides rect).
     */
    QImage
    crop(const QImage &input, const QRect &rect);

    /**
     * Crop image to a polygon (4 corners, for perspective-corrected crops).
     * TODO not implemented here (WarpDetection module)
     */
    QImage
    crop(const QImage &input, const QPolygonF &quad);

    QImage
    process(const QImage &input) override;

    QString
    name() const override;

    bool
    isAvailable() const override;

};

#endif //PROCESSING_CROP_PROCESSOR_HPP

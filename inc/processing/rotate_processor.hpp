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

#ifndef PROCESSING_ROTATE_PROCESSOR_HPP
#define PROCESSING_ROTATE_PROCESSOR_HPP

#include "processing/image_processor.hpp"
#include <QColor>

/**
 * Rotate processor - rotates images by specified angles.
 * Always available (Qt-only, no external dependencies).
 * 
 * Handles both manual rotation (user clicks "Rotate 90°" button) and
 * automatic rotation (angle from BorderDetector's skew detection).
 */
class RotateProcessor : public ImageProcessor
{
public:

    RotateProcessor();

    /**
     * Rotate by exact angle (for manual "Rotate 90°/180°/270°" buttons).
     * Only supports 0, 90, 180, 270 degrees.
     */
    QImage
    rotate(const QImage &input, int degrees);

    /**
     * Rotate by arbitrary angle with background fill (for deskew).
     * Used for automatic skew correction (e.g., -2.3°, 1.5°).
     * @param degrees Rotation angle in degrees (positive = clockwise)
     * @param fill Background color for areas outside the rotated image
     */
    QImage
    rotate(const QImage &input, double degrees, QColor fill = Qt::white);

    /**
     * Convenience method: no rotation (returns copy).
     */
    QImage
    process(const QImage &input) override;

    QString
    name() const override;

    bool
    isAvailable() const override;

};

#endif // PROCESSING_ROTATE_PROCESSOR_HPP

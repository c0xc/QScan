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

#ifndef PROCESSING_BORDER_DETECTOR_HPP
#define PROCESSING_BORDER_DETECTOR_HPP

#include <QRect>
#include <QVector>

#include "processing/image_processor.hpp"

/**
 * Border detector - detects content bounds and skew in scanned images.
 * Always available (Qt-only, no external dependencies).
 * 
 * This is the "auto" part of auto-crop and auto-rotate for scanner output.
 * Works by detecting white/uniform borders around the actual document content.
 * 
 * Usage:
 *   BorderDetector detector;
 *   QRect contentRect = detector.detectContentBounds(scannedImage);
 *   CropProcessor cropper;
 *   QImage cropped = cropper.crop(scannedImage, contentRect);
 */
class BorderDetector : public ImageProcessor
{
public:

    struct ContentBounds
    {
        QRect best;
        QVector<QRect> candidates; //TODO
        bool confident;

        ContentBounds()
            : best(),
              candidates(),
              confident(false)
        {
        }
    };

    BorderDetector();

    /**
     * Detect content bounds in a scanned image (white border removal).
     * Analyzes the image to find where the actual content starts/ends.
     * Returns the rectangle containing the actual content.
     * 
     * Algorithm: Scans from edges inward to find where pixel values
     * deviate from the background (white or black).
     */
    QRect
    detectContentBounds(const QImage &input);

    ContentBounds
    detectBorders(const QImage &input);

    /**
     * Detect rotation angle (slight deskew).
     * Analyzes the image to detect if it's slightly rotated.
     * Returns angle in degrees (e.g., -2.3, 0.5).
     * Positive values = clockwise rotation needed to correct.
     * 
     * Algorithm: Projects pixel rows at various angles, finds the angle
     * that produces the sharpest horizontal lines.
     */
    double
    detectSkewAngle(const QImage &input);

    /**
     * Convenience: auto-crop (detect + crop in one call).
     * Returns the input image cropped to detected content bounds.
     */
    QImage
    process(const QImage &input) override;

    QString
    name() const override;

    bool
    isAvailable() const override;

};

#endif //PROCESSING_BORDER_DETECTOR_HPP

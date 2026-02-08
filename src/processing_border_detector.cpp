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

#include "processing/border_detector.hpp"

BorderDetector::BorderDetector()
{
}

QRect
BorderDetector::detectContentBounds(const QImage &input)
{
    // TODO: Implement white border detection
    // Algorithm: Scan from edges inward to find where pixel values deviate from background
    // For now, return the entire image bounds
    return input.rect();
}

double
BorderDetector::detectSkewAngle(const QImage &input)
{
    // TODO: Implement skew detection
    // Algorithm: Project pixel rows at various angles, find angle with sharpest peaks
    // For now, return 0 (no skew detected)
    return 0.0;
}

QImage
BorderDetector::process(const QImage &input)
{
    // Convenience: detect + crop
    QRect bounds = detectContentBounds(input);
    return input.copy(bounds);
}

QString
BorderDetector::name() const
{
    return "Border Detector";
}

bool
BorderDetector::isAvailable() const
{
    return true;  // Always available (Qt-only)
}

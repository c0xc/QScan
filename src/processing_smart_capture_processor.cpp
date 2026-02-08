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

#include "processing/smart_capture_processor.hpp"

#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

SmartCaptureProcessor::SmartCaptureProcessor()
{
}

DetectedRegion
SmartCaptureProcessor::detectDocument(const QImage &input)
{
#ifdef HAVE_OPENCV
    // TODO: Implement OpenCV-based document detection
    // Algorithm:
    // 1. Convert QImage to cv::Mat
    // 2. Convert to grayscale
    // 3. Apply Canny edge detection
    // 4. Find contours
    // 5. Filter for quadrilaterals
    // 6. Select largest quad with reasonable aspect ratio
    // 7. Convert back to QPolygonF
    
    DetectedRegion region;
    region.confidence = 0.0;  // Not implemented yet
    return region;
#else
    // OpenCV not available
    DetectedRegion region;
    region.confidence = 0.0;
    return region;
#endif
}

QImage
SmartCaptureProcessor::extractDocument(const QImage &input, const DetectedRegion &region)
{
#ifdef HAVE_OPENCV
    // TODO: Implement OpenCV-based perspective correction
    // Algorithm:
    // 1. Convert QImage to cv::Mat
    // 2. Calculate perspective transform matrix from 4 corners
    // 3. Apply warpPerspective to flatten the document
    // 4. Convert back to QImage
    
    return input.copy();  // Stub: return original
#else
    // OpenCV not available - return simple bounding box crop
    if (region.isValid()) {
        QRectF bounds = region.corners.boundingRect();
        return input.copy(bounds.toRect());
    }
    return input.copy();
#endif
}

bool
SmartCaptureProcessor::isAvailable() const
{
#ifdef HAVE_OPENCV
    return true;
#else
    return false;
#endif
}

QImage
SmartCaptureProcessor::process(const QImage &input)
{
    // Convenience: detect + extract in one step
    DetectedRegion region = detectDocument(input);
    if (region.isValid()) {
        return extractDocument(input, region);
    }
    return input.copy();
}

QString
SmartCaptureProcessor::name() const
{
    return "Smart Capture";
}

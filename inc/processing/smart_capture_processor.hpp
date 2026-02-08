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

#ifndef PROCESSING_SMART_CAPTURE_PROCESSOR_HPP
#define PROCESSING_SMART_CAPTURE_PROCESSOR_HPP

#include "processing/image_processor.hpp"
#include <QObject>
#include <QPolygonF>

/**
 * Represents a detected document region in a photo.
 * Contains 4 corners (possibly non-rectangular due to perspective).
 */
struct DetectedRegion {
    QPolygonF corners;      // 4 corners in clockwise order: top-left, top-right, bottom-right, bottom-left
    double confidence;      // 0.0 - 1.0, how confident the detection is
    
    DetectedRegion() : confidence(0.0) {}
    
    bool isValid() const {
        return confidence > 0.5 && corners.size() == 4;
    }
};

/**
 * Smart Capture processor - detects and extracts documents from photos.
 * Optional feature (requires OpenCV with core + imgproc modules).
 * 
 * This processor handles the "webcam smart capture" workflow:
 * 1. User takes a photo with webcam (document on desk, possibly at an angle)
 * 2. detectDocument() finds the document edges and returns a polygon
 * 3. GUI overlays the polygon as an editable rectangle on the preview
 * 4. User adjusts corners if needed and confirms
 * 5. extractDocument() performs perspective correction and crops
 * 
 * Without OpenCV: isAvailable() returns false, feature is disabled in GUI.
 * With OpenCV: Uses cv::findContours(), cv::approxPolyDP(), cv::getPerspectiveTransform(),
 *              and cv::warpPerspective() for robust document detection and extraction.
 */
class SmartCaptureProcessor : public QObject, public ImageProcessor
{
    Q_OBJECT

public:

    SmartCaptureProcessor();

    // --- Detection (called by GUI to get the rectangle) ---

    /**
     * Detect document region in a photo/frame.
     * Analyzes the image to find a quadrilateral that likely represents a document.
     * Returns detected region that GUI can overlay as a rectangle.
     * 
     * Algorithm (with OpenCV):
     * - Convert to grayscale
     * - Apply edge detection (Canny)
     * - Find contours
     * - Filter for quadrilaterals
     * - Select largest quadrilateral with reasonable aspect ratio
     * 
     * @param input Photo/frame from webcam
     * @return DetectedRegion with 4 corners and confidence score
     */
    DetectedRegion
    detectDocument(const QImage &input);

    // --- Extraction (called after user confirms the rectangle) ---

    /**
     * Extract and perspective-correct the document from the detected region.
     * Takes the original image and a region (possibly adjusted by user),
     * applies perspective transformation to flatten the document,
     * and returns a cropped, rectangular image.
     * 
     * Algorithm (with OpenCV):
     * - Calculate perspective transform matrix from 4 corners
     * - Apply warp perspective to flatten the document
     * - Crop to target rectangle
     * 
     * @param input Original photo/frame
     * @param region Detected region (4 corners), possibly user-adjusted
     * @return Flattened, cropped document image
     */
    QImage
    extractDocument(const QImage &input, const DetectedRegion &region);

    // --- Capability ---

    /**
     * Check if SmartCapture is available.
     * Returns false if built without OpenCV.
     */
    bool
    isAvailable() const override;

    // --- Base interface ---

    /**
     * Convenience method: detect + extract in one step (for batch/auto mode).
     * Automatically detects the document and extracts it.
     */
    QImage
    process(const QImage &input) override;

    QString
    name() const override;

signals:

    /**
     * Emitted when detection updates (for real-time webcam mode).
     * GUI can connect to this signal to update the overlay rectangle
     * as the user positions the document.
     */
    void
    regionDetected(const DetectedRegion &region);

};

#endif // PROCESSING_SMART_CAPTURE_PROCESSOR_HPP

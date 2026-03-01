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

#ifndef PROCESSING_IMAGE_PROCESSOR_HPP
#define PROCESSING_IMAGE_PROCESSOR_HPP

#include <QImage>
#include <QString>
#include <QFlags>

/**
 * Base interface for image processing operations.
 * All OpenCV-based processing is isolated in this layer.
 * GUI layer never directly calls OpenCV functions.
 */
class ImageProcessor
{
public:

    /**
     * Processing capabilities available in the application.
     * Some capabilities require optional dependencies (e.g., OpenCV).
     */
    enum Capability {
        Crop            = 0x01,   //always available (CropProcessor)
        Rotate          = 0x02,   //always available (RotateProcessor)
        Enhance         = 0x04,   //always available (EnhanceProcessor)
        BorderDetect    = 0x08,   //always available (BorderDetector, Qt-only)
        SmartCapture    = 0x10,   //optional (SmartCaptureProcessor, needs OpenCV)
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    virtual
    ~ImageProcessor() = default;

    /**
     * Process an image and return the result.
     * This is a convenience method for simple one-step processing.
     */
    virtual QImage
    process(const QImage &input) = 0;

    /**
     * Get processor name for display/logging.
     */
    virtual QString
    name() const = 0;

    /**
     * Check if this processor is available (dependencies met).
     */
    virtual bool
    isAvailable() const = 0;

    /**
     * Get all available processing capabilities in this build.
     * Returns a combination of Capability flags.
     */
    static Capabilities
    availableCapabilities();

};

Q_DECLARE_OPERATORS_FOR_FLAGS(ImageProcessor::Capabilities)

#endif //PROCESSING_IMAGE_PROCESSOR_HPP

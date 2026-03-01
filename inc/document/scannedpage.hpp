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

#ifndef DOCUMENT_SCANNEDPAGE_HPP
#define DOCUMENT_SCANNEDPAGE_HPP

#include <QImage>
#include <QPixmap>
#include <QDateTime>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantMap>

/**
 * Represents a single scanned page with its image and metadata.
 * Stores the raw image plus transformation info (rotation, crop).
 * Some of those attributes may be unused, unnecessary or bogus.
 */
class ScannedPage
{
public:

    enum class AcquisitionKind //TODO remove
    {
        Scan,
        Preview
    };

    ScannedPage();

    ScannedPage(const QImage &img);

    void
    setAcquisitionKind(AcquisitionKind kind); //TODO remove

    AcquisitionKind
    acquisitionKind() const;

    void
    setAutoCropApplied(bool applied);

    bool
    autoCropApplied() const;

    /**
     * Set the raw scanned image.
     */
    void
    setImage(const QImage &img);

    //Optional: keep an unmodified original image for camera input
    //This is stored to avoid throwing away raw data too early
    void
    setOriginalImage(const QImage &img);

    bool
    hasOriginalImage() const;

    QImage
    originalImage() const;

    //Optional: cache a perspective-warped image for camera input
    void
    setWarpedImage(const QImage &img);

    bool
    hasWarpedImage() const;

    QImage
    warpedImage() const;

    /**
     * Get the raw scanned image (without transformations).
     */
    QImage
    rawImage() const;

    /**
     * Get the processed image with rotation and crop applied.
     */
    QImage
    processedImage() const;

    /**
    * Get the image in edit space.
    * Rotation applied, crop not applied.
    * Used for preview and crop overlay editing.
    * 
    * The caller should not need to know which transforms exist.
     */
    QImage
    currentImage() const;

    /**
    * Compatibility alias for currentImage.
    */
    QImage
    rotatedImage() const; //TODO

    QSize
    pixelSize() const;

    int
    imageDepthBits() const;

    int
    bytesPerLine() const;

    bool
    hasAlphaChannel() const;

    bool
    isGrayscale() const;

    int
    imageFormat() const;

    QString
    imageFormatName() const; //TODO

    QString
    imageColorSummary() const; //TODO

    /**
     * Set rotation angle in degrees (0, 90, 180, 270).
     */
    void
    setRotation(int degrees);

    /**
     * Get rotation angle.
     */
    int
    rotation() const;

    /**
     * Set crop rectangle in normalized coordinates (0.0-1.0).
     */
    void
    setCropRect(const QRectF &rect);

    /**
     * Get crop rectangle.
     */
    QRectF
    cropRect() const;

    /**
     * Generate a thumbnail of the processed image.
     */
    QPixmap
    thumbnail(const QSize &size) const;

    /**
     * Set scan timestamp.
     */
    void
    setScanTime(const QDateTime &time);

    /**
     * Get scan timestamp.
     */
    QDateTime
    scanTime() const;

    /**
     * Set source scanner name.
     */
    void
    setSourceName(const QString &name);

    /**
     * Get source scanner name.
     */
    QString
    sourceName() const;

    void
    setSourceDescription(const QString &description);

    QString
    sourceDescription() const;

    void
    setScanResolutionDpi(int dpi);

    int
    scanResolutionDpi() const;

    void
    setScanColorMode(const QString &mode);

    QString
    scanColorMode() const;

    void
    setScanAreaMm(const QSizeF &mm_size);

    QSizeF
    scanAreaMm() const;

    void
    setAutoPageSize(bool enabled);

    bool
    autoPageSize() const;

    void
    setUsedAdf(bool enabled);

    bool
    usedAdf() const;

    void
    setUsedDuplex(bool enabled);

    bool
    usedDuplex() const;

    void
    setReportedDocumentSizeMm(const QSizeF &mm_size, const QString &paper_name);

    bool
    hasReportedDocumentSize() const;

    QSizeF
    reportedDocumentSizeMm() const;

    QString
    reportedPaperName() const;

    void
    setBackendKind(const QString &kind);

    QString
    backendKind() const;

    void
    setBackendDetails(const QVariantMap &details);

    QVariantMap
    backendDetails() const;

    void
    setEffectiveResolutionDpi(int dpi);

    bool
    hasEffectiveResolutionDpi() const;

    int
    effectiveResolutionDpi() const;

    void
    setEffectiveColorMode(const QString &mode);

    bool
    hasEffectiveColorMode() const;

    QString
    effectiveColorMode() const;

private:

    QImage m_image;
    QImage m_original_image;
    QImage m_warped_image;
    int m_rotation;         //0, 90, 180, 270
    QRectF m_crop_rect;     //normalized (0.0-1.0)
    QDateTime m_scan_time;
    QString m_source_name;
    QString m_source_description;

    AcquisitionKind m_acquisition_kind;
    bool m_auto_crop_applied;

    int m_scan_resolution_dpi;
    QString m_scan_color_mode;
    QSizeF m_scan_area_mm;
    bool m_auto_page_size;
    bool m_used_adf;
    bool m_used_duplex;

    bool m_reported_document_size_valid;
    QSizeF m_reported_document_size_mm;
    QString m_reported_paper_name;

    QString m_backend_kind;
    QVariantMap m_backend_details;

    bool m_effective_resolution_valid;
    int m_effective_resolution_dpi;

    bool m_effective_color_mode_valid;
    QString m_effective_color_mode;

};

#endif //DOCUMENT_SCANNEDPAGE_HPP

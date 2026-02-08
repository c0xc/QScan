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

#ifndef DOCUMENT_SCANNEDPAGE_HPP
#define DOCUMENT_SCANNEDPAGE_HPP

#include <QImage>
#include <QPixmap>
#include <QDateTime>
#include <QRectF>
#include <QString>

/**
 * Represents a single scanned page with its image and metadata.
 * Stores the raw image plus transformation info (rotation, crop).
 */
class ScannedPage
{
public:

    ScannedPage();

    ScannedPage(const QImage &img);

    /**
     * Set the raw scanned image.
     */
    void
    setImage(const QImage &img);

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

private:

    QImage m_image;
    int m_rotation;         // 0, 90, 180, 270
    QRectF m_crop_rect;     // Normalized (0.0-1.0)
    QDateTime m_scan_time;
    QString m_source_name;

};

#endif // DOCUMENT_SCANNEDPAGE_HPP

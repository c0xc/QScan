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

#ifndef SCAN_SCANCAPABILITIES_HPP
#define SCAN_SCANCAPABILITIES_HPP

#include <QList>
#include <QString>
#include <QSizeF>

/**
 * Preview mode supported by the scan source.
 */
enum class PreviewMode
{
    None,           // No preview support
    SingleImage,    // Preview is a single scan (traditional scanners)
    LiveStream      // Preview is a continuous video stream (webcams)
};

/**
 * Describes the capabilities of a scan source.
 * Used by GUI to show/hide features dynamically.
 */
struct ScanCapabilities
{
    bool supports_multi_page;          // Can scan multiple pages
    bool supports_auto_feed;           // Has automatic document feeder (ADF)
    bool supports_color_mode;          // Can scan in color
    bool supports_auto_page_size;      // Can auto-detect page dimensions
    bool supports_scan_settings;       // Whether scan parameter controls (resolution, page size, etc.) are meaningful
    QList<int> supported_resolutions;  // Available DPI values
    QStringList supported_color_modes; // "Color", "Gray", "BW"
    QSizeF max_scan_area;              // Maximum scan area in mm
    QSizeF default_scan_area;          // Default scan area in mm (typically A4)
    PreviewMode preview_mode;          // Type of preview this source supports

    /**
     * Constructor with sensible defaults
     */
    ScanCapabilities()
        : supports_multi_page(false)
        , supports_auto_feed(false)
        , supports_color_mode(true)
        , supports_auto_page_size(false)
        , supports_scan_settings(true)
        , max_scan_area(210.0, 297.0)      // A4
        , default_scan_area(210.0, 297.0)  // A4
        , preview_mode(PreviewMode::SingleImage)
    {
        supported_resolutions << 75 << 150 << 300 << 600;
        supported_color_modes << "Color" << "Gray" << "BW";
    }

    /**
     * Helper to check if source provides live preview stream.
     */
    bool
    hasLivePreview() const
    {
        return preview_mode == PreviewMode::LiveStream;
    }
};

/**
 * Parameters for a scan operation.
 */
struct ScanParameters
{
    int resolution;        // DPI
    QString color_mode;    // "Color", "Gray", or "BW"
    QSizeF scan_area;      // Scan area in mm (0,0 = auto-detect if supported)
    bool use_adf;          // Use automatic document feeder
    bool auto_page_size;   // Use automatic page size detection

    ScanParameters()
        : resolution(300)
        , color_mode("Color")
        , scan_area(210.0, 297.0) // A4
        , use_adf(false)
        , auto_page_size(false)
    {
    }
};

#endif // SCAN_SCANCAPABILITIES_HPP

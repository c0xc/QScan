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

#ifndef SCAN_SCAN_CAPABILITIES_HPP
#define SCAN_SCAN_CAPABILITIES_HPP

#include <QList>
#include <QString>
#include <QSizeF>

enum class PreviewMode
{
    None,           //no preview support
    SingleImage,    //preview is a single scan (traditional scanners)
    LiveStream      //preview is a continuous video stream (webcams)
};

struct ScanCapabilities
{
    bool supports_multi_page;
    bool supports_auto_feed;
    bool supports_duplex;
    bool supports_color_mode;
    bool supports_auto_page_size;
    bool supports_scan_settings;
    QList<int> supported_resolutions;
    QStringList supported_color_modes;
    QStringList supported_input_sources;
    QStringList supported_scan_sides;
    QSizeF max_scan_area;
    QSizeF default_scan_area;
    PreviewMode preview_mode;

    ScanCapabilities()
        : supports_multi_page(false)
        , supports_auto_feed(false)
        , supports_duplex(false)
        , supports_color_mode(true)
        , supports_auto_page_size(false)
        , supports_scan_settings(true)
        , max_scan_area(210.0, 297.0) //A4 portrait in mm
        , default_scan_area(210.0, 297.0) //A4 portrait in mm
        , preview_mode(PreviewMode::SingleImage)
    {
        //Common scan DPI presets
        supported_resolutions << 75 << 150 << 300 << 600;
        supported_color_modes << "Color" << "Gray" << "BW";
    }

    bool
    hasLivePreview() const
    {
        return preview_mode == PreviewMode::LiveStream;
    }
};

struct ScanParameters
{
    int resolution;
    QString color_mode;
    QSizeF scan_area;
    bool use_adf;
    bool use_duplex;
    bool auto_page_size;

    ScanParameters()
        : resolution(300) //typical default for document scans
        , color_mode("Color")
        , scan_area(210.0, 297.0) //A4 portrait in mm
        , use_adf(false)
        , use_duplex(false)
        , auto_page_size(false)
    {
    }
};

#endif //SCAN_SCAN_CAPABILITIES_HPP

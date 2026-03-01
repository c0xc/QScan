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

#ifndef GUI_SCANCONTROLPANEL_HPP
#define GUI_SCANCONTROLPANEL_HPP

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QToolButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>

#include "scan/scan_capabilities.hpp"

/**
 * Control panel widget containing scan settings and image processing controls.
 * Displayed on the right side of the main window.
 */
class ScanControlPanel : public QWidget
{
    Q_OBJECT

public:

    enum CropMode
    {
        CropOff = 0,
        CropAuto = 1,
        CropManual = 2
    };

    explicit
    ScanControlPanel(QWidget *parent = 0);

    void
    setCapabilities(const ScanCapabilities &caps);

    ScanParameters
    getScanParameters() const;

    void
    setScanControlsEnabled(bool enabled);

    void
    setCropMode(CropMode mode);

    CropMode
    cropMode() const;

    void
    setAutoCropWarning(const QString &tooltip, bool enabled);


signals:

    void
    rotateLeftRequested();

    void
    rotateRightRequested();

    void
    cropModeChanged(int mode);

    void
    nextCropRequested();

    void
    adfModeChanged(bool enabled);

private slots:

    void
    onAdvancedToggled();

    void
    onPageSizeChanged(int index);

    void
    onAdfToggled(bool enabled);

private:

    //Scan Settings
    QGroupBox *m_scan_settings_group;
    QComboBox *m_resolution_combo;
    QCheckBox *m_adf_checkbox;
    QCheckBox *m_duplex_checkbox;
    QComboBox *m_color_mode_combo;
    QPushButton *m_advanced_button;

    //Advanced scan settings
    QWidget *m_advanced_widget;
    QComboBox *m_page_size_combo;
    bool m_advanced_visible;

    //Image Processing
    QGroupBox *m_image_processing_group;
    QToolButton *m_crop_off_button;
    QToolButton *m_crop_auto_button;
    QToolButton *m_crop_manual_button;
    QButtonGroup *m_crop_mode_group;
    bool m_crop_auto_reclick_pending;
    QPushButton *m_rotate_left_button;
    QPushButton *m_rotate_right_button;

    //Current capabilities
    ScanCapabilities m_capabilities;

    void
    setupUi();

    void
    populateResolutions();

    void
    populateColorModes();

    void
    populatePageSizes();
};

#endif //GUI_SCANCONTROLPANEL_HPP

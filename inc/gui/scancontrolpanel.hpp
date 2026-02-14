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

#ifndef GUI_SCANCONTROLPANEL_HPP
#define GUI_SCANCONTROLPANEL_HPP

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>

#include "scan/scancapabilities.hpp"

/**
 * Control panel widget containing scan settings and image processing controls.
 * Displayed on the right side of the main window.
 */
class ScanControlPanel : public QWidget
{
    Q_OBJECT

public:

    explicit
    ScanControlPanel(QWidget *parent = nullptr);

    /**
     * Update controls based on scanner capabilities.
     * Called when a scanner is initialized.
     */
    void
    setCapabilities(const ScanCapabilities &caps);

    /**
     * Get current scan parameters from UI controls.
     */
    ScanParameters
    getScanParameters() const;

    /**
     * Enable/disable scan controls (e.g., during scanning).
     */
    void
    setScanControlsEnabled(bool enabled);

signals:

    /**
     * Emitted when user clicks rotate left button.
     */
    void
    rotateLeftRequested();

    /**
     * Emitted when user clicks rotate right button.
     */
    void
    rotateRightRequested();

    /**
     * Emitted when user clicks manual crop button.
     */
    void
    manualCropRequested();

    /**
     * Emitted when auto-crop checkbox state changes.
     */
    void
    autoCropChanged(bool enabled);

    /**
     * Emitted when ADF checkbox state changes.
     */
    void
    adfModeChanged(bool enabled);

private slots:

    void
    onAdvancedToggled();

    void
    onPageSizeChanged(int index);

private:

    // Scan Settings
    QGroupBox *m_scan_settings_group;
    QComboBox *m_resolution_combo;
    QCheckBox *m_adf_checkbox;
    QComboBox *m_color_mode_combo;
    QPushButton *m_advanced_button;
    
    // Advanced scan settings (hidden by default)
    QWidget *m_advanced_widget;
    QComboBox *m_page_size_combo;
    bool m_advanced_visible;

    // Image Processing
    QGroupBox *m_image_processing_group;
    QCheckBox *m_autocrop_checkbox;
    QPushButton *m_rotate_left_button;
    QPushButton *m_rotate_right_button;
    QPushButton *m_manual_crop_button;

    // Current capabilities
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

#endif // GUI_SCANCONTROLPANEL_HPP

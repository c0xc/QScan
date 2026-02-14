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

#include "gui/scancontrolpanel.hpp"
#include <QFormLayout>
#include <QHBoxLayout>

ScanControlPanel::ScanControlPanel(QWidget *parent)
                : QWidget(parent),
                  m_advanced_visible(false)
{
    setupUi();
}

void
ScanControlPanel::setCapabilities(const ScanCapabilities &caps)
{
    m_capabilities = caps;
    
    // Show/hide entire scan settings group based on capability
    m_scan_settings_group->setVisible(caps.supports_scan_settings);
    
    if (caps.supports_scan_settings)
    {
        // Update resolution combo
        populateResolutions();
        
        // Update color mode combo
        populateColorModes();
        
        // Update page size combo
        populatePageSizes();
        
        // Show/hide ADF checkbox based on capability
        if (caps.supports_auto_feed)
        {
            m_adf_checkbox->setVisible(true);
            m_adf_checkbox->setEnabled(true);
        }
        else
        {
            m_adf_checkbox->setVisible(false);
            m_adf_checkbox->setChecked(false);
        }
    }
}

ScanParameters
ScanControlPanel::getScanParameters() const
{
    ScanParameters params;
    
    // Resolution
    params.resolution = m_resolution_combo->currentData().toInt();
    
    // Color mode
    params.color_mode = m_color_mode_combo->currentText();
    
    // ADF
    params.use_adf = m_adf_checkbox->isChecked();
    
    // Page size
    int page_size_index = m_page_size_combo->currentIndex();
    if (page_size_index == 0 && m_capabilities.supports_auto_page_size)
    {
        // Auto-detect
        params.auto_page_size = true;
        params.scan_area = QSizeF(0, 0);
    }
    else
    {
        params.auto_page_size = false;
        params.scan_area = m_page_size_combo->currentData().toSizeF();
    }
    
    return params;
}

void
ScanControlPanel::setScanControlsEnabled(bool enabled)
{
    m_resolution_combo->setEnabled(enabled);
    m_adf_checkbox->setEnabled(enabled && m_capabilities.supports_auto_feed);
    m_color_mode_combo->setEnabled(enabled);
    m_advanced_button->setEnabled(enabled);
    m_page_size_combo->setEnabled(enabled);
}

void
ScanControlPanel::onAdvancedToggled()
{
    m_advanced_visible = !m_advanced_visible;
    m_advanced_widget->setVisible(m_advanced_visible);
    
    if (m_advanced_visible)
    {
        m_advanced_button->setText(tr("Advanced ▲"));
    }
    else
    {
        m_advanced_button->setText(tr("Advanced ▼"));
    }
}

void
ScanControlPanel::onPageSizeChanged(int index)
{
    // Could add logic here if needed
    Q_UNUSED(index);
}

void
ScanControlPanel::setupUi()
{
    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(12);
    
    // === Scan Settings Group ===
    m_scan_settings_group = new QGroupBox(tr("Scan Settings"));
    QVBoxLayout *scan_layout = new QVBoxLayout(m_scan_settings_group);
    
    QFormLayout *form_layout = new QFormLayout;
    form_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    
    // Resolution
    m_resolution_combo = new QComboBox;
    form_layout->addRow(tr("Resolution:"), m_resolution_combo);
    
    // Color Mode
    m_color_mode_combo = new QComboBox;
    form_layout->addRow(tr("Color Mode:"), m_color_mode_combo);
    
    // ADF checkbox
    m_adf_checkbox = new QCheckBox(tr("Scan from feeder (ADF)"));
    m_adf_checkbox->setVisible(false); // Hidden until capabilities set
    connect(m_adf_checkbox, SIGNAL(toggled(bool)),
            this, SIGNAL(adfModeChanged(bool)));
    form_layout->addRow("", m_adf_checkbox);
    
    scan_layout->addLayout(form_layout);
    
    // Advanced button
    m_advanced_button = new QPushButton(tr("Advanced ▼"));
    m_advanced_button->setFlat(true);
    connect(m_advanced_button, SIGNAL(clicked()),
            this, SLOT(onAdvancedToggled()));
    scan_layout->addWidget(m_advanced_button);
    
    // Advanced settings (hidden by default)
    m_advanced_widget = new QWidget;
    QFormLayout *advanced_layout = new QFormLayout(m_advanced_widget);
    
    m_page_size_combo = new QComboBox;
    connect(m_page_size_combo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onPageSizeChanged(int)));
    advanced_layout->addRow(tr("Page Size:"), m_page_size_combo);
    
    m_advanced_widget->setVisible(false);
    scan_layout->addWidget(m_advanced_widget);
    
    main_layout->addWidget(m_scan_settings_group);
    
    // === Image Processing Group ===
    m_image_processing_group = new QGroupBox(tr("Image Processing"));
    QVBoxLayout *processing_layout = new QVBoxLayout(m_image_processing_group);
    
    // Auto-crop checkbox
    m_autocrop_checkbox = new QCheckBox(tr("Auto-crop borders"));
    connect(m_autocrop_checkbox, SIGNAL(toggled(bool)),
            this, SIGNAL(autoCropChanged(bool)));
    processing_layout->addWidget(m_autocrop_checkbox);
    
    // Rotation buttons
    QHBoxLayout *rotate_layout = new QHBoxLayout;
    m_rotate_left_button = new QPushButton(tr("↺ Rotate Left"));
    m_rotate_right_button = new QPushButton(tr("↻ Rotate Right"));
    connect(m_rotate_left_button, SIGNAL(clicked()),
            this, SIGNAL(rotateLeftRequested()));
    connect(m_rotate_right_button, SIGNAL(clicked()),
            this, SIGNAL(rotateRightRequested()));
    rotate_layout->addWidget(m_rotate_left_button);
    rotate_layout->addWidget(m_rotate_right_button);
    processing_layout->addLayout(rotate_layout);
    
    // Manual crop button
    m_manual_crop_button = new QPushButton(tr("Manual Crop..."));
    m_manual_crop_button->setEnabled(false); // TODO: Enable when implemented
    connect(m_manual_crop_button, SIGNAL(clicked()),
            this, SIGNAL(manualCropRequested()));
    processing_layout->addWidget(m_manual_crop_button);
    
    main_layout->addWidget(m_image_processing_group);
    
    // Add stretch to push everything to the top
    main_layout->addStretch();
    
    // Set minimum width
    setMinimumWidth(220);
}

void
ScanControlPanel::populateResolutions()
{
    m_resolution_combo->clear();
    
    if (m_capabilities.supported_resolutions.isEmpty())
    {
        // Default resolutions
        m_resolution_combo->addItem("75 DPI", 75);
        m_resolution_combo->addItem("150 DPI", 150);
        m_resolution_combo->addItem("300 DPI", 300);
        m_resolution_combo->addItem("600 DPI", 600);
        m_resolution_combo->setCurrentIndex(2); // 300 DPI
    }
    else
    {
        foreach (int dpi, m_capabilities.supported_resolutions)
        {
            m_resolution_combo->addItem(QString("%1 DPI").arg(dpi), dpi);
            if (dpi == 300)
            {
                m_resolution_combo->setCurrentIndex(m_resolution_combo->count() - 1);
            }
        }
    }
}

void
ScanControlPanel::populateColorModes()
{
    m_color_mode_combo->clear();
    
    if (m_capabilities.supported_color_modes.isEmpty())
    {
        m_color_mode_combo->addItem("Color");
        m_color_mode_combo->addItem("Gray");
        m_color_mode_combo->addItem("BW");
    }
    else
    {
        foreach (const QString &mode, m_capabilities.supported_color_modes)
        {
            m_color_mode_combo->addItem(mode);
        }
    }
    
    // Default to Color
    int color_index = m_color_mode_combo->findText("Color");
    if (color_index >= 0)
    {
        m_color_mode_combo->setCurrentIndex(color_index);
    }
}

void
ScanControlPanel::populatePageSizes()
{
    m_page_size_combo->clear();
    
    // Add Auto option if supported
    if (m_capabilities.supports_auto_page_size)
    {
        m_page_size_combo->addItem(tr("Auto-detect"), QSizeF(0, 0));
    }
    
    // Standard page sizes
    m_page_size_combo->addItem("A4 (210×297 mm)", QSizeF(210.0, 297.0));
    m_page_size_combo->addItem("Letter (216×279 mm)", QSizeF(216.0, 279.0));
    m_page_size_combo->addItem("Legal (216×356 mm)", QSizeF(216.0, 356.0));
    m_page_size_combo->addItem("A5 (148×210 mm)", QSizeF(148.0, 210.0));
    
    // Select A4 by default (or Auto if available)
    m_page_size_combo->setCurrentIndex(0);
}

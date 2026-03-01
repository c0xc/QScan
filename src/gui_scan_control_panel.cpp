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

#include <QFormLayout>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QLabel>

#include "gui/scancontrolpanel.hpp"

ScanControlPanel::ScanControlPanel(QWidget *parent)
                : QWidget(parent),
                  m_advanced_visible(false),
                  m_crop_auto_reclick_pending(false)
{
    setupUi();
}

void
ScanControlPanel::onAdfToggled(bool enabled)
{
    //Duplex is only meaningful for feeder scans
    m_duplex_checkbox->setEnabled(enabled && m_capabilities.supports_duplex);
    if (!enabled)
        m_duplex_checkbox->setChecked(false);
}

void
ScanControlPanel::setCapabilities(const ScanCapabilities &caps)
{
    m_capabilities = caps;

    //Show/hide entire scan settings group based on capability
    m_scan_settings_group->setVisible(caps.supports_scan_settings);

    if (caps.supports_scan_settings)
    {
        //Update resolution combo
        populateResolutions();

        //Update color mode combo
        populateColorModes();

        //Update page size combo
        populatePageSizes();

        //Show/hide ADF checkbox based on capability
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

        //Show/hide Duplex checkbox based on capability
        if (caps.supports_duplex)
        {
            m_duplex_checkbox->setVisible(true);
        }
        else
        {
            m_duplex_checkbox->setVisible(false);
            m_duplex_checkbox->setChecked(false);
        }

        //Duplex only makes sense with ADF
        onAdfToggled(m_adf_checkbox->isChecked());
    }
}

ScanParameters
ScanControlPanel::getScanParameters() const
{
    ScanParameters params;

    //Resolution
    params.resolution = m_resolution_combo->currentData().toInt();

    //Color mode
    params.color_mode = m_color_mode_combo->currentText();

    //ADF
    params.use_adf = m_adf_checkbox->isChecked();

    //Duplex (only effective when ADF is enabled)
    params.use_duplex = m_duplex_checkbox->isChecked() && params.use_adf;

    //Page size
    //Auto-detect is represented by QSizeF(0,0)
    const QSizeF selected_area = m_page_size_combo->currentData().toSizeF();
    if (m_capabilities.supports_auto_page_size && selected_area.isEmpty())
    {
        params.auto_page_size = true;
        params.scan_area = QSizeF();
    }
    else
    {
        params.auto_page_size = false;
        params.scan_area = selected_area;
    }

    return params;
}

void
ScanControlPanel::setScanControlsEnabled(bool enabled)
{
    m_resolution_combo->setEnabled(enabled);
    m_adf_checkbox->setEnabled(enabled && m_capabilities.supports_auto_feed);
    m_duplex_checkbox->setEnabled(enabled && m_capabilities.supports_duplex && m_adf_checkbox->isChecked());
    m_color_mode_combo->setEnabled(enabled);
    m_advanced_button->setEnabled(enabled);
    m_page_size_combo->setEnabled(enabled);

    if (m_crop_off_button) m_crop_off_button->setEnabled(enabled);
    if (m_crop_auto_button) m_crop_auto_button->setEnabled(enabled);
    if (m_crop_manual_button) m_crop_manual_button->setEnabled(enabled);
}

void
ScanControlPanel::setCropMode(CropMode mode)
{
    if (!m_crop_mode_group)
        return;

    QAbstractButton *b = m_crop_mode_group->button((int)mode);
    if (!b)
        return;

    QSignalBlocker blocker(m_crop_mode_group);
    b->setChecked(true);
}

ScanControlPanel::CropMode
ScanControlPanel::cropMode() const
{
    if (!m_crop_mode_group)
        return CropOff;

    const int id = m_crop_mode_group->checkedId();
    if (id == (int)CropAuto) return CropAuto;
    if (id == (int)CropManual) return CropManual;
    return CropOff;
}

void
ScanControlPanel::setAutoCropWarning(const QString &tooltip, bool enabled)
{
    if (!m_crop_auto_button)
        return;

    m_crop_auto_button->setProperty("qscanAutoWarn", enabled);
    m_crop_auto_button->setToolTip(tooltip);
    m_crop_auto_button->style()->unpolish(m_crop_auto_button);
    m_crop_auto_button->style()->polish(m_crop_auto_button);
    m_crop_auto_button->update();
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
    //Not implemented
    Q_UNUSED(index);
}

void
ScanControlPanel::setupUi()
{
    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(12);

    //=== Scan Settings Group ===
    m_scan_settings_group = new QGroupBox(tr("Scan Settings"));
    QVBoxLayout *scan_layout = new QVBoxLayout(m_scan_settings_group);

    QFormLayout *form_layout = new QFormLayout;
    form_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    //Resolution
    m_resolution_combo = new QComboBox;
    form_layout->addRow(tr("Resolution:"), m_resolution_combo);

    //Color Mode
    m_color_mode_combo = new QComboBox;
    form_layout->addRow(tr("Color Mode:"), m_color_mode_combo);

    //ADF checkbox
    m_adf_checkbox = new QCheckBox(tr("Scan from feeder (ADF)"));
    m_adf_checkbox->setVisible(false); //hidden until capabilities set
    connect(m_adf_checkbox, SIGNAL(toggled(bool)),
            this, SIGNAL(adfModeChanged(bool)));
    connect(m_adf_checkbox, SIGNAL(toggled(bool)),
            this, SLOT(onAdfToggled(bool)));
    form_layout->addRow("", m_adf_checkbox);

    //Duplex checkbox
    m_duplex_checkbox = new QCheckBox(tr("Duplex (both sides)"));
    m_duplex_checkbox->setVisible(false); //hidden until capabilities set
    m_duplex_checkbox->setEnabled(false);
    form_layout->addRow("", m_duplex_checkbox);

    scan_layout->addLayout(form_layout);

    //Advanced button
    m_advanced_button = new QPushButton(tr("Advanced ▼"));
    m_advanced_button->setFlat(true);
    connect(m_advanced_button, SIGNAL(clicked()),
            this, SLOT(onAdvancedToggled()));
    scan_layout->addWidget(m_advanced_button);

    //Advanced settings (hidden by default)
    m_advanced_widget = new QWidget;
    QFormLayout *advanced_layout = new QFormLayout(m_advanced_widget);

    m_page_size_combo = new QComboBox;
    connect(m_page_size_combo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onPageSizeChanged(int)));
    advanced_layout->addRow(tr("Page Size:"), m_page_size_combo);

    m_advanced_widget->setVisible(false);
    scan_layout->addWidget(m_advanced_widget);

    main_layout->addWidget(m_scan_settings_group);

    //=== Crop Group ===
    QGroupBox *crop_group = new QGroupBox(tr("Crop"));
    QVBoxLayout *crop_group_layout = new QVBoxLayout(crop_group);

    //Crop mode selector (compact segmented control)
    QWidget *crop_row = new QWidget;
    QHBoxLayout *crop_layout = new QHBoxLayout(crop_row);
    crop_layout->setContentsMargins(0, 0, 0, 0);
    crop_layout->setSpacing(0);

    const auto apply_crop_segment_sizing = [](QToolButton *button)
    {
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QFontMetrics fm(button->font());
        button->setMinimumWidth(fm.horizontalAdvance(button->text()) + 30);
    };

    m_crop_mode_group = new QButtonGroup(this);
    m_crop_mode_group->setExclusive(true);

    m_crop_off_button = new QToolButton;
    m_crop_off_button->setText(tr("Off"));
    m_crop_off_button->setCheckable(true);
    m_crop_off_button->setProperty("qscanCropSeg", true);
    m_crop_off_button->setProperty("qscanCropFirst", true);
    apply_crop_segment_sizing(m_crop_off_button);
    m_crop_mode_group->addButton(m_crop_off_button, (int)CropOff);
    crop_layout->addWidget(m_crop_off_button);

    m_crop_auto_button = new QToolButton;
    m_crop_auto_button->setText(tr("Auto"));
    m_crop_auto_button->setCheckable(true);
    m_crop_auto_button->setProperty("qscanCropSeg", true);
    apply_crop_segment_sizing(m_crop_auto_button);
    m_crop_mode_group->addButton(m_crop_auto_button, (int)CropAuto);
    crop_layout->addWidget(m_crop_auto_button);

    m_crop_manual_button = new QToolButton;
    m_crop_manual_button->setText(tr("Manual"));
    m_crop_manual_button->setCheckable(true);
    m_crop_manual_button->setProperty("qscanCropSeg", true);
    m_crop_manual_button->setProperty("qscanCropLast", true);
    apply_crop_segment_sizing(m_crop_manual_button);
    m_crop_mode_group->addButton(m_crop_manual_button, (int)CropManual);
    crop_layout->addWidget(m_crop_manual_button);

    m_crop_auto_button->setToolTip(tr("Auto crop (click again to try next candidate)"));
    connect(m_crop_auto_button, &QToolButton::pressed, this, [this]()
    {
        m_crop_auto_reclick_pending = (cropMode() == CropAuto);
    });
    connect(m_crop_auto_button, &QToolButton::clicked, this, [this]()
    {
        if (m_crop_auto_reclick_pending)
            emit nextCropRequested();
    });

    crop_row->setStyleSheet(
        "QToolButton[qscanCropSeg=\"true\"] {"
        "  border: 1px solid palette(mid);"
        "  padding: 4px 10px;"
        "  margin: 0px;"
        "}"
        "QToolButton[qscanCropFirst=\"true\"] {"
        "  border-top-left-radius: 4px;"
        "  border-bottom-left-radius: 4px;"
        "  border-right: 0px;"
        "}"
        "QToolButton[qscanCropLast=\"true\"] {"
        "  border-top-right-radius: 4px;"
        "  border-bottom-right-radius: 4px;"
        "  border-left: 0px;"
        "}"
        "QToolButton[qscanCropSeg=\"true\"]:checked {"
        "  background-color: palette(highlight);"
        "  color: palette(highlighted-text);"
        "}"
        "QToolButton[qscanAutoWarn=\"true\"] {"
        "  border: 2px solid palette(highlight);"
        "  font-weight: bold;"
        "}"
    );

    connect(m_crop_mode_group, &QButtonGroup::idClicked, this, [this](int id)
    {
        if (id == (int)CropAuto && m_crop_auto_reclick_pending)
            return;
        emit cropModeChanged(id);
    });

    //Default selection
    m_crop_off_button->setChecked(true);
    setAutoCropWarning(QString(), false);

    crop_group_layout->addWidget(crop_row);

    main_layout->addWidget(crop_group);

    //=== Image Processing Group ===
    m_image_processing_group = new QGroupBox(tr("Image Processing"));
    QVBoxLayout *processing_layout = new QVBoxLayout(m_image_processing_group);

    //Rotation buttons
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

    main_layout->addWidget(m_image_processing_group);

    //Add stretch to push everything to the top
    main_layout->addStretch();

    //Set minimum width
    setMinimumWidth(220);
}

void
ScanControlPanel::populateResolutions()
{
    m_resolution_combo->clear();

    if (m_capabilities.supported_resolutions.isEmpty())
    {
        //Default resolutions
        m_resolution_combo->addItem("75 DPI", 75);
        m_resolution_combo->addItem("150 DPI", 150);
        m_resolution_combo->addItem("300 DPI", 300);
        m_resolution_combo->addItem("600 DPI", 600);
        m_resolution_combo->setCurrentIndex(2); //300 DPI
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

    //Default to Color
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

    //Add Auto option if supported
    if (m_capabilities.supports_auto_page_size)
    {
        m_page_size_combo->addItem(tr("Auto-detect"), QSizeF(0, 0));
    }

    //Standard page sizes (ISO 216)
    m_page_size_combo->addItem("A4 (210×297 mm)", QSizeF(210.0, 297.0));
    m_page_size_combo->addItem("A5 (148×210 mm)", QSizeF(148.0, 210.0));
    m_page_size_combo->addItem("A6 (105×148 mm)", QSizeF(105.0, 148.0));
    m_page_size_combo->addItem("A3 (297×420 mm)", QSizeF(297.0, 420.0));
    m_page_size_combo->addItem("A2 (420×594 mm)", QSizeF(420.0, 594.0));
    m_page_size_combo->addItem("A1 (594×841 mm)", QSizeF(594.0, 841.0));
    m_page_size_combo->addItem("A0 (841×1189 mm)", QSizeF(841.0, 1189.0));

    //Common non-ISO sizes
    m_page_size_combo->addItem("Legal (216×356 mm)", QSizeF(216.0, 356.0));

    //Select A4 by default (or Auto if available)
    m_page_size_combo->setCurrentIndex(0);
}

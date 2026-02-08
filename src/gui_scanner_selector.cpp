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

#include "gui/scannerselector.hpp"

ScannerSelector::ScannerSelector(ScanManager *manager, QWidget *parent)
               : QDialog(parent),
                 m_scan_manager(manager),
                 m_selected_mode(Document::IMAGE_MODE)
{
    setWindowTitle(tr("Select Scanner"));
    setupUi();
    
    // Populate device list
    QList<ScanDeviceInfo> devices = m_scan_manager->availableDevices();
    foreach (const ScanDeviceInfo &dev, devices)
    {
        QString display = dev.description + " (" + dev.typeString() + ")";
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, dev.name);
        m_device_list->addItem(item);
    }
    
    // Select first device if available
    if (m_device_list->count() > 0)
    {
        m_device_list->setCurrentRow(0);
        onDeviceSelectionChanged();
    }
}

QString
ScannerSelector::selectedDeviceName() const
{
    return m_selected_device;
}

Document::ScanMode
ScannerSelector::selectedMode() const
{
    return m_selected_mode;
}

void
ScannerSelector::onDeviceSelectionChanged()
{
    QListWidgetItem *item = m_device_list->currentItem();
    if (item)
    {
        m_selected_device = item->data(Qt::UserRole).toString();
        updateCapabilitiesDisplay();
        m_btn_ok->setEnabled(true);
    }
    else
    {
        m_selected_device.clear();
        m_btn_ok->setEnabled(false);
    }
}

void
ScannerSelector::onOkClicked()
{
    // Get selected mode
    if (m_rb_document_mode->isChecked())
    {
        m_selected_mode = Document::DOCUMENT_MODE;
    }
    else
    {
        m_selected_mode = Document::IMAGE_MODE;
    }
    
    accept();
}

void
ScannerSelector::setupUi()
{
    QVBoxLayout *main_layout = new QVBoxLayout(this);
    
    // Device list
    QLabel *lbl_devices = new QLabel(tr("Available Devices:"));
    main_layout->addWidget(lbl_devices);
    
    m_device_list = new QListWidget;
    main_layout->addWidget(m_device_list);
    connect(m_device_list, SIGNAL(currentRowChanged(int)),
            this, SLOT(onDeviceSelectionChanged()));
    
    // Capabilities display
    m_capabilities_label = new QLabel;
    m_capabilities_label->setWordWrap(true);
    main_layout->addWidget(m_capabilities_label);
    
    // Scan mode selection
    QLabel *lbl_mode = new QLabel(tr("Scan Mode:"));
    main_layout->addWidget(lbl_mode);
    
    m_rb_image_mode = new QRadioButton(tr("Image Mode (single scan, JPG/PNG)"));
    m_rb_image_mode->setChecked(true);
    main_layout->addWidget(m_rb_image_mode);
    
    m_rb_document_mode = new QRadioButton(tr("Document Mode (multi-page, PDF)"));
    main_layout->addWidget(m_rb_document_mode);
    
    // Buttons
    QHBoxLayout *button_layout = new QHBoxLayout;
    m_btn_ok = new QPushButton(tr("OK"));
    m_btn_ok->setEnabled(false);
    connect(m_btn_ok, SIGNAL(clicked()), this, SLOT(onOkClicked()));
    button_layout->addWidget(m_btn_ok);
    
    m_btn_cancel = new QPushButton(tr("Cancel"));
    connect(m_btn_cancel, SIGNAL(clicked()), this, SLOT(reject()));
    button_layout->addWidget(m_btn_cancel);
    
    main_layout->addLayout(button_layout);
    
    resize(500, 400);
}

void
ScannerSelector::updateCapabilitiesDisplay()
{
    // TODO: Query capabilities from selected device and display
    m_capabilities_label->setText(tr("Device selected. Click OK to continue."));
}

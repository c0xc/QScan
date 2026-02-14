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
#include <QIcon>
#include <QStyle>

ScannerSelector::ScannerSelector(ScanManager *manager, QWidget *parent)
               : QDialog(parent),
                 m_scan_manager(manager)
{
    setWindowTitle(tr("Select Scanner"));
    setupUi();
    
    // Populate device list with type icons
    QList<ScanDeviceInfo> devices = m_scan_manager->availableDevices();
    foreach (const ScanDeviceInfo &dev, devices)
    {
        QString display = dev.description;
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, dev.name);
        item->setData(Qt::UserRole + 1, (int)dev.type);
        
        // Add type icon and label
        if (dev.type == ScanDeviceType::SCANNER)
        {
            // Use printer icon for scanner
            QIcon icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
            item->setIcon(icon);
            item->setText(QString("🖨️ %1").arg(display));
        }
        else if (dev.type == ScanDeviceType::CAMERA)
        {
            // Use camera icon
            item->setText(QString("📷 %1").arg(display));
        }
        
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
    m_device_list->setIconSize(QSize(24, 24));
    main_layout->addWidget(m_device_list);
    connect(m_device_list, SIGNAL(currentRowChanged(int)),
            this, SLOT(onDeviceSelectionChanged()));
    connect(m_device_list, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this, SLOT(onOkClicked()));
    
    // Capabilities display
    m_capabilities_label = new QLabel;
    m_capabilities_label->setWordWrap(true);
    m_capabilities_label->setStyleSheet("QLabel { color: #666; font-size: 10pt; }");
    main_layout->addWidget(m_capabilities_label);
    
    // Buttons
    QHBoxLayout *button_layout = new QHBoxLayout;
    button_layout->addStretch();
    
    m_btn_ok = new QPushButton(tr("OK"));
    m_btn_ok->setEnabled(false);
    m_btn_ok->setDefault(true);
    connect(m_btn_ok, SIGNAL(clicked()), this, SLOT(onOkClicked()));
    button_layout->addWidget(m_btn_ok);
    
    m_btn_cancel = new QPushButton(tr("Cancel"));
    connect(m_btn_cancel, SIGNAL(clicked()), this, SLOT(reject()));
    button_layout->addWidget(m_btn_cancel);
    
    main_layout->addLayout(button_layout);
    
    resize(500, 350);
}

void
ScannerSelector::updateCapabilitiesDisplay()
{
    QListWidgetItem *item = m_device_list->currentItem();
    if (!item)
    {
        m_capabilities_label->clear();
        return;
    }
    
    ScanDeviceType type = (ScanDeviceType)item->data(Qt::UserRole + 1).toInt();
    
    QString info;
    if (type == ScanDeviceType::SCANNER)
    {
        info = tr("Scanner device selected. Click OK to continue.");
    }
    else if (type == ScanDeviceType::CAMERA)
    {
        info = tr("Camera device selected. Click OK to continue.");
    }
    else
    {
        info = tr("Device selected. Click OK to continue.");
    }
    
    m_capabilities_label->setText(info);
}

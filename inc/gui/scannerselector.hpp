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

#ifndef GUI_SCANNERSELECTOR_HPP
#define GUI_SCANNERSELECTOR_HPP

#include <QDialog>
#include <QListWidget>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "scan/scanmanager.hpp"
#include "document/document.hpp"

/**
 * Initial dialog for selecting scanner device and scan mode.
 * Shown at application startup before main window.
 */
class ScannerSelector : public QDialog
{
    Q_OBJECT

public:

    explicit
    ScannerSelector(ScanManager *manager, QWidget *parent = nullptr);

    /**
     * Get the device name selected by user.
     */
    QString
    selectedDeviceName() const;

private slots:

    void
    onDeviceSelectionChanged();

    void
    onOkClicked();

private:

    ScanManager *m_scan_manager;
    QString m_selected_device;

    // UI elements
    QListWidget *m_device_list;
    QLabel *m_capabilities_label;
    QPushButton *m_btn_ok;
    QPushButton *m_btn_cancel;

    void
    setupUi();

    void
    updateCapabilitiesDisplay();

};

#endif // GUI_SCANNERSELECTOR_HPP

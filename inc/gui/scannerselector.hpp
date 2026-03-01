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

#ifndef GUI_SCANNERSELECTOR_HPP
#define GUI_SCANNERSELECTOR_HPP

#include <QDialog>
#include <QListWidget>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolButton>
#include <QWidget>

#include "scan/scan_manager.hpp"
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
    ScannerSelector(ScanManager *manager, QWidget *parent = 0);

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

    //UI elements
    QListWidget *m_device_list;
    QLabel *m_capabilities_label;
    QPushButton *m_btn_ok;
    QPushButton *m_btn_cancel;

    //Manual sources
    QToolButton *m_btn_add_source;
    QPushButton *m_btn_remove_source;

    //eSCL add panel (hidden until Add Source...)
    QWidget *m_escl_panel;
    QLineEdit *m_escl_url_edit;
    QPushButton *m_btn_escl_test;
    QLabel *m_escl_status;
    QLineEdit *m_escl_label_edit;
    QPushButton *m_btn_escl_save;
    QPushButton *m_btn_escl_cancel;
    QString m_escl_tested_url;

    void
    setupUi();

    void
    populateDeviceList();

    void
    restoreLastSelection();

    void
    updateCapabilitiesDisplay();

private slots:

    void
    onAddEsclSourceRequested();

    void
    onEsclTestClicked();

    void
    onEsclSaveClicked();

    void
    onEsclCancelClicked();

    void
    onRemoveNetworkScannerClicked();

};

#endif //GUI_SCANNERSELECTOR_HPP

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

#include "main.hpp"

#include <QGuiApplication>
#include <QIcon>
#include <QStyle>

int
main(int argc, char *argv[])
{
    //Initialize Qt Application
    QApplication app(argc, argv);
    app.setApplicationName(PROGRAM); //QScan
    app.setOrganizationName("c0xc");

    //Set a deterministic app/window icon
    //Many desktop environments use this for taskbar/dock icons
    {
        QGuiApplication::setDesktopFileName(QStringLiteral("qscan"));

        QIcon icon(QStringLiteral(":/icons/QScan_17719790191e44_1.png"));
        if (icon.isNull())
            icon = QIcon(QStringLiteral(":/icons/icons/scanner.svg"));
        if (icon.isNull())
            icon = app.style()->standardIcon(QStyle::SP_DesktopIcon);
        app.setWindowIcon(icon);
    }

    Debug(QS("Qt compile version: %s", QT_VERSION_STR));
    Debug(QS("Qt runtime version: %s", qVersion()));

#if defined(USE_GSTREAMER)
    Debug(QS("Webcam backend compiled in: GStreamer"));
#endif
#if defined(USE_QTCAMERA)
    Debug(QS("Webcam backend compiled in: QtCamera"));
#endif
#if !defined(USE_GSTREAMER) && !defined(USE_QTCAMERA)
    Debug(QS("Webcam backend compiled in: none"));
#endif

    qRegisterMetaType<qscan::ScanPageInfo>("qscan::ScanPageInfo");

    //Initialize scan manager
    ScanManager scan_manager;
    if (!scan_manager.initialize())
    {
        QMessageBox::critical(0, QObject::tr("Initialization Error"),
                            QObject::tr("Failed to initialize scan manager."));
        return 1;
    }

    if (scan_manager.availableDevices().isEmpty())
        Debug(QS("No devices enumerated at startup; showing selector for manual add"));

    //Show scanner selection dialog
    ScannerSelector selector(&scan_manager);
    if (selector.exec() != QDialog::Accepted)
    {
        return 0;  //User cancelled
    }

    //Create scan source
    QString device_name = selector.selectedDeviceName();
    Debug(QS("Selected device: <%s>", CSTR(device_name)));

    ScanSource *scan_source = scan_manager.createScanSource(device_name);
    if (!scan_source)
    {
        Debug(QS("FAILED to create scan source for device <%s>", CSTR(device_name)));
        QMessageBox::critical(0, QObject::tr("Device Error"),
                            QObject::tr("Failed to open device: %1").arg(device_name));
        return 1;
    }

    Debug(QS("Calling initialize() on scan source for device <%s>", CSTR(device_name)));
    while (!scan_source->initialize())
    {
        Debug(QS("FAILED to initialize scan source for device <%s>", CSTR(device_name)));

        QString text = QObject::tr("Failed to initialize device.");
        if (device_name.startsWith("hpaio:"))
        {
            text += "\n\n" + QObject::tr(
                "Tip of the day: This looks like an HP (hpaio) device. "
                "If HP's plugin/driver installer is blocking scans, resolve that first and then retry (HP Device Manager). "
                "If HP's plugin software gives you a bad day, consider adding the scanner as eSCL as a workaround (Add Source...).");
        }

        QMessageBox box(QMessageBox::Critical, QObject::tr("Device Error"), text, QMessageBox::NoButton, &selector);
        QPushButton *retry = box.addButton(QObject::tr("Retry"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();

        if (box.clickedButton() != retry)
        {
            delete scan_source;
            return 1;
        }
    }
    Debug(QS("Scan source initialized successfully for device <%s>", CSTR(device_name)));

    //Create and show main window
    MainWindow *main_window = new MainWindow(scan_source);
    main_window->show();

    //Run event loop
    int code = app.exec();
    
    //Cleanup
    delete main_window;
    delete scan_source;
    
    return code;
}


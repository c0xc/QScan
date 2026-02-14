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

int
main(int argc, char *argv[])
{
    // Initialize Qt Application
    QApplication app(argc, argv);
    app.setApplicationName(PROGRAM);
    app.setOrganizationName("QScan");

    // Initialize scan manager
    ScanManager scan_manager;
    if (!scan_manager.initialize())
    {
        QMessageBox::critical(0, QObject::tr("Initialization Error"),
                            QObject::tr("Failed to initialize scan manager."));
        return 1;
    }

    // Check if any devices are available
    if (scan_manager.availableDevices().isEmpty())
    {
        QMessageBox::warning(0, QObject::tr("No Devices"),
                           QObject::tr("No scanner or webcam devices found.\n\n"
                                      "Please connect a scanner or webcam and try again."));
        return 1;
    }

    // Show scanner selection dialog
    ScannerSelector selector(&scan_manager);
    if (selector.exec() != QDialog::Accepted)
    {
        return 0;  // User cancelled
    }

    // Create scan source
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
    if (!scan_source->initialize())
    {
        Debug(QS("FAILED to initialize scan source for device <%s>", CSTR(device_name)));
        QMessageBox::critical(0, QObject::tr("Device Error"),
                            QObject::tr("Failed to initialize device."));
        delete scan_source;
        return 1;
    }
    Debug(QS("Scan source initialized successfully for device <%s>", CSTR(device_name)));

    // Create and show main window
    MainWindow *main_window = new MainWindow(scan_source);
    main_window->show();

    // Run event loop
    int code = app.exec();
    
    // Cleanup
    delete main_window;
    delete scan_source;
    
    return code;

    // TODO: Implement full SANE scanning and webcam capture
    /*
    // Initialize scan manager
    ScanManager scan_manager;
    if (!scan_manager.initialize())
    {
        QMessageBox::critical(0, QObject::tr("Initialization Error"),
                            QObject::tr("Failed to initialize SANE. Please ensure SANE is properly installed."));
        return 1;
    }

    // Check if any devices are available
    if (scan_manager.availableDevices().isEmpty())
    {
        QMessageBox::warning(0, QObject::tr("No Devices"),
                           QObject::tr("No scanner devices found. Please connect a scanner and try again."));
        return 1;
    }

    // Show scanner selection dialog
    ScannerSelector selector(&scan_manager);
    if (selector.exec() != QDialog::Accepted)
    {
        return 0;  // User cancelled
    }

    // Create scan source
    QString device_name = selector.selectedDeviceName();
    Document::ScanMode scan_mode = selector.selectedMode();
    
    ScanSource *scan_source = scan_manager.createScanSource(device_name);
    if (!scan_source)
    {
        QMessageBox::critical(0, QObject::tr("Device Error"),
                            QObject::tr("Failed to open scanner device: %1").arg(device_name));
        return 1;
    }

    if (!scan_source->initialize())
    {
        QMessageBox::critical(0, QObject::tr("Device Error"),
                            QObject::tr("Failed to initialize scanner device."));
        delete scan_source;
        return 1;
    }

    // Create and show main window
    MainWindow *main_window = new MainWindow(scan_source, scan_mode);
    main_window->show();

    // Run event loop
    int code = app.exec();
    
    // Cleanup
    delete main_window;
    delete scan_source;
    
    return code;
    */
}


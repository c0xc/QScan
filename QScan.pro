# QScan
# qmake config - for Qt5 compatibility
# possibly reduced feature set (as opposed to our cmake config)

QT += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = qscan
TEMPLATE = app

# C++ Standard
CONFIG += c++11

# Directories
INCLUDEPATH += inc
DEPENDPATH += inc

# SANE library
unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += sane-backends
}

# OpenCV library (optional - comment out to disable SmartCapture)
# unix {
#     CONFIG += link_pkgconfig
#     PKGCONFIG += opencv4
#     DEFINES += HAVE_OPENCV=1
# }

# Source files
SOURCES += \
    src/main.cpp \
    src/core_classlogger.cpp \
    src/core_settingsmanager.cpp \
    src/core_profilesettings.cpp \
    src/processing_image_processor.cpp \
    src/processing_crop_processor.cpp \
    src/processing_rotate_processor.cpp \
    src/processing_border_detector.cpp \
    src/processing_smart_capture_processor.cpp \
    src/scan_scanner_source.cpp \
    src/scan_scanner_backend_sane.cpp \
    src/scan_webcam_source.cpp \
    src/scan_mobile_source.cpp \
    src/scan_scan_manager.cpp \
    src/document_scanned_page.cpp \
    src/document_document.cpp \
    src/document_document_exporter.cpp \
    src/gui_scanner_selector.cpp \
    src/gui_mainwindow.cpp \
    src/gui_scan_preview_widget.cpp \
    src/gui_page_list_widget.cpp \
    src/gui_scan_control_panel.cpp

# Header files
HEADERS += \
    inc/main.hpp \
    inc/core/classlogger.hpp \
    inc/core/settingsmanager.hpp \
    inc/core/profilesettings.hpp \
    inc/scan/scan_capabilities.hpp \
    inc/scan/scan_device_info.hpp \
    inc/scan/scan_source.hpp \
    inc/scan/scanner_backend.hpp \
    inc/scan/scanner_source.hpp \
    inc/scan/webcam_backend.hpp \
    inc/scan/webcam_source.hpp \
    inc/scan/scan_manager.hpp \
    inc/scan/mobile_source.hpp \
    inc/document/scannedpage.hpp \
    inc/document/document.hpp \
    inc/document/documentexporter.hpp \
    inc/processing/image_processor.hpp \
    inc/processing/crop_processor.hpp \
    inc/processing/rotate_processor.hpp \
    inc/processing/border_detector.hpp \
    inc/processing/smart_capture_processor.hpp \
    inc/gui/scannerselector.hpp \
    inc/gui/mainwindow.hpp \
    inc/gui/scanpreviewwidget.hpp \
    inc/gui/pagelistwidget.hpp \
    inc/gui/scancontrolpanel.hpp

# Installation
target.path = /usr/local/bin
INSTALLS += target

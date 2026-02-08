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
    src/core_settingsmanager.cpp \
    src/core_profilesettings.cpp \
    src/processing_image_processor.cpp \
    src/processing_crop_processor.cpp \
    src/processing_rotate_processor.cpp \
    src/processing_border_detector.cpp
    # src/processing_smart_capture_processor.cpp  # Only if OpenCV enabled
    
    # Scan backend - stubs to be implemented
    # src/scan_sanescandevice.cpp \
    # src/scan_webcamsource.cpp \
    # src/scan_scanmanager.cpp \
    
    # Document model - stubs to be implemented
    # src/document_scannedpage.cpp \
    # src/document_document.cpp \
    # src/document_documentexporter.cpp \
    
    # GUI - stubs to be implemented
    # src/gui_scannerselector.cpp \
    # src/gui_mainwindow.cpp \
    # src/gui_scanpreviewwidget.cpp \
    # src/gui_pagelistwidget.cpp

# Header files
HEADERS += \
    inc/main.hpp \
    inc/core/settingsmanager.hpp \
    inc/core/profilesettings.hpp \
    \
    # Scan backend
    inc/scan/scancapabilities.hpp \
    inc/scan/scansource.hpp \
    inc/scan/sanescandevice.hpp \
    inc/scan/webcamsource.hpp \
    inc/scan/scanmanager.hpp \
    \
    # Document model
    inc/document/scannedpage.hpp \
    inc/document/document.hpp \
    inc/document/documentexporter.hpp \
    \
    # Processing
    inc/processing/image_processor.hpp \
    inc/processing/crop_processor.hpp \
    inc/processing/rotate_processor.hpp \
    inc/processing/border_detector.hpp \
    inc/processing/smart_capture_processor.hpp \
    \
    # GUI
    inc/gui/scannerselector.hpp \
    inc/gui/mainwindow.hpp \
    inc/gui/scanpreviewwidget.hpp \
    inc/gui/pagelistwidget.hpp

# Installation
target.path = /usr/local/bin
INSTALLS += target

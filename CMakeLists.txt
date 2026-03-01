# QScan - A "Linux first" scanning application -- [c0xc], 2026

cmake_minimum_required(VERSION 3.10)
project(QScan VERSION 1.0.0 LANGUAGES CXX)

#Warnings
#Keep helper utilities around even if not wired yet.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-unused-function)
endif()

# C++ Standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt (try Qt6 first, fall back to Qt5)
find_package(Qt6 COMPONENTS Core Gui Widgets Network QUIET)
if(Qt6_FOUND)
    message(STATUS "Found Qt6")
    set(QT_VERSION_MAJOR 6)
    set(QT_FOUND TRUE)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets Network QUIET)
    if(Qt5_FOUND)
        message(STATUS "Found Qt5")
        set(QT_VERSION_MAJOR 5)
        set(QT_FOUND TRUE)
    else()
        message(FATAL_ERROR "Neither Qt6 nor Qt5 found. Please install Qt development packages.")
    endif()
endif()

# Optional QtPdf support (PDF import)
# NOTE: QtPdf (Qt's PDF module) is used to READ/RENDER PDFs into images for import
#       This is unrelated to the "qpdf" library (lowercase), which is used below only
#       for OPTIONAL metadata injection into exported PDFs
set(QSCAN_HAVE_QTPDF OFF)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 COMPONENTS Pdf QUIET)
    if(TARGET Qt6::Pdf)
        set(QSCAN_HAVE_QTPDF ON)
    endif()
else()
    find_package(Qt5 COMPONENTS Pdf QUIET)
    if(TARGET Qt5::Pdf)
        set(QSCAN_HAVE_QTPDF ON)
    endif()
endif()

# Optional QtSvg support (SVG icons)
# NOTE: QScan ships a few SVG icons in the Qt resource file
set(QSCAN_HAVE_QTSVG OFF)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 COMPONENTS Svg QUIET)
    if(TARGET Qt6::Svg)
        set(QSCAN_HAVE_QTSVG ON)
    endif()
else()
    find_package(Qt5 COMPONENTS Svg QUIET)
    if(TARGET Qt5::Svg)
        set(QSCAN_HAVE_QTSVG ON)
    endif()
endif()

if(QSCAN_HAVE_QTSVG)
    message(STATUS "QtSvg enabled: SVG icons supported")
else()
    message(STATUS "QtSvg disabled: SVG icons may not render")
endif()

if(QSCAN_HAVE_QTPDF)
    message(STATUS "QtPdf enabled: PDF import via QtPdf")
    add_compile_definitions(QSCAN_HAVE_QTPDF=1)
else()
    message(STATUS "QtPdf disabled: PDF import not available")
endif()

# GStreamer support for webcam backend - optional, fallback would be Qt6
option(QSCAN_ENABLE_GSTREAMER "Enable GStreamer for webcam capture (cross-platform)" OFF)

# Default QtCamera backend (Qt Multimedia) - At6 only
set(QSCAN_HAVE_QTCAMERA OFF)
if(QT_VERSION_MAJOR EQUAL 6 AND NOT QSCAN_ENABLE_GSTREAMER)
    find_package(Qt6 COMPONENTS Multimedia QUIET)
    if(TARGET Qt6::Multimedia)
        set(QSCAN_HAVE_QTCAMERA ON)
    endif()
endif()

if(QSCAN_HAVE_QTCAMERA)
    message(STATUS "QtCamera enabled: webcam capture via Qt Multimedia")
    add_compile_definitions(USE_QTCAMERA=1)
else()
    if(QSCAN_ENABLE_GSTREAMER)
        message(STATUS "GStreamer enabled (using GStreamer for webcam capture)")
    else()
        message(STATUS "QtCamera disabled: Qt6 Multimedia not found") # this would be a build error
    endif()
endif()

# Find SANE
find_package(PkgConfig REQUIRED)
pkg_check_modules(SANE REQUIRED sane-backends)

# Optional qpdf library (PDF metadata post-processing)
# NOTE: This is the external "qpdf" library, NOT QtPdf
# Default OFF to avoid adding a runtime lib dependency for local builds.
option(QSCAN_ENABLE_QPDF "Enable QPDF for richer PDF metadata" OFF)
if(QSCAN_ENABLE_QPDF)
    pkg_check_modules(QPDF QUIET libqpdf)
    if(QPDF_FOUND)
        message(STATUS "QPDF enabled: PDF metadata injection")
        add_compile_definitions(HAVE_QPDF=1)
        include_directories(${QPDF_INCLUDE_DIRS})
    else()
        message(STATUS "QPDF not found: Extra PDF metadata not supported")
    endif()
endif()

if(QSCAN_ENABLE_GSTREAMER)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
    if(GSTREAMER_FOUND)
        message(STATUS "GStreamer enabled: webcam capture via GStreamer")
        add_compile_definitions(USE_GSTREAMER=1)
        include_directories(${GSTREAMER_INCLUDE_DIRS})
    endif()
endif()

# OpenCV support for image processing like camera auto-crop - optional
option(QSCAN_ENABLE_OPENCV "Enable OpenCV for advanced image processing" OFF)

if(QSCAN_ENABLE_OPENCV)
    # Only require core and imgproc modules (not the full OpenCV suite)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV enabled: Advanced image processing")
        add_compile_definitions(HAVE_OPENCV=1)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/inc
    ${SANE_INCLUDE_DIRS}
)

if(QSCAN_ENABLE_OPENCV)
    include_directories(${OpenCV_INCLUDE_DIRS})
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/core_classlogger.cpp
    src/core_settingsmanager.cpp
    src/core_profilesettings.cpp
    
    # Processing - always available (Qt-only)
    src/processing_image_processor.cpp
    src/processing_crop_processor.cpp
    src/processing_rotate_processor.cpp
    src/processing_border_detector.cpp
    
    # Document model
    src/document_scanned_page.cpp
    src/document_document.cpp
    src/document_document_exporter.cpp
    src/document_pdf_importer.cpp
    
    # Scan backend
    src/scan_scan_manager.cpp
    src/scan_scanner_source.cpp
    src/scan_scanner_source_additions.cpp
    src/scan_scanner_backend_sane.cpp
    src/scan_document_size.cpp
    src/scan_scanner_backend_escl.cpp
    src/scan_escl_probe.cpp
    src/scan_escl_compatibility_detector.cpp
    src/scan_escl_url.cpp
    src/scan_webcam_source.cpp
    src/scan_mobile_source.cpp
    
    # GUI
    src/gui_mainwindow.cpp
    src/gui_scan_preview_widget.cpp
    src/gui_page_list_widget.cpp
    src/gui_scanner_selector.cpp
    src/gui_scan_control_panel.cpp

    resources/qscan_icons.qrc
)

# Webcam backend implementations (selected by configuration)
if(QSCAN_ENABLE_GSTREAMER)
    list(APPEND SOURCES src/scan_webcam_source_gstreamer.cpp)
endif()

if(QSCAN_HAVE_QTCAMERA)
    list(APPEND SOURCES src/scan_webcam_source_qtcamera.cpp)
endif()

# Always build SmartCaptureProcessor
# It compiles without OpenCV (stubbed) and reports isAvailable()=false
list(APPEND SOURCES src/processing_smart_capture_processor.cpp)

# Header files with Q_OBJECT that need MOC processing
set(HEADERS_WITH_MOC
    inc/document/document.hpp
    inc/scan/scan_source.hpp
    inc/scan/scanner_source.hpp
    inc/scan/webcam_source.hpp
    inc/scan/scan_manager.hpp
    inc/scan/mobile_source.hpp
    inc/processing/smart_capture_processor.hpp
    inc/gui/mainwindow.hpp
    inc/gui/scanpreviewwidget.hpp
    inc/gui/pagelistwidget.hpp
    inc/gui/scannerselector.hpp
    inc/gui/scancontrolpanel.hpp
)

# Create executable
add_executable(qscan ${SOURCES} ${HEADERS_WITH_MOC})

# Link libraries
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(qscan
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        $<$<BOOL:${QSCAN_HAVE_QTPDF}>:Qt6::Pdf>
        $<$<BOOL:${QSCAN_HAVE_QTSVG}>:Qt6::Svg>
        $<$<BOOL:${QSCAN_HAVE_QTCAMERA}>:Qt6::Multimedia>
        ${SANE_LIBRARIES}
    )
else()
    target_link_libraries(qscan
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
        $<$<BOOL:${QSCAN_HAVE_QTPDF}>:Qt5::Pdf>
        $<$<BOOL:${QSCAN_HAVE_QTSVG}>:Qt5::Svg>
        $<$<BOOL:${QSCAN_HAVE_QTCAMERA}>:Qt5::Multimedia>
        ${SANE_LIBRARIES}
    )
endif()

# Link OpenCV only if enabled
if(QSCAN_ENABLE_OPENCV)
    target_link_libraries(qscan ${OpenCV_LIBS})
endif()

# Link GStreamer only if enabled
if(QSCAN_ENABLE_GSTREAMER)
    target_link_libraries(qscan ${GSTREAMER_LIBRARIES})
    target_link_directories(qscan PRIVATE ${GSTREAMER_LIBRARY_DIRS})
endif()

# Link QPDF only if enabled+found
if(QSCAN_ENABLE_QPDF AND QPDF_FOUND)
    target_link_libraries(qscan ${QPDF_LIBRARIES})
    target_link_directories(qscan PRIVATE ${QPDF_LIBRARY_DIRS})
endif()

# Link directories
target_link_directories(qscan PRIVATE ${SANE_LIBRARY_DIRS})

# Installation
install(TARGETS qscan DESTINATION bin)

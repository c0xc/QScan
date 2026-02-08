cmake_minimum_required(VERSION 3.10)
project(QScan VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt (try Qt6 first, fall back to Qt5)
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Found Qt6")
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    message(STATUS "Found Qt5")
    set(QT_VERSION_MAJOR 5)
endif()

# Find SANE
find_package(PkgConfig REQUIRED)
pkg_check_modules(SANE REQUIRED sane-backends)

# Optional GStreamer support for webcam capture (cross-platform)
option(QSCAN_ENABLE_GSTREAMER "Enable GStreamer for webcam capture (cross-platform)" OFF)

if(QSCAN_ENABLE_GSTREAMER)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)
    if(GSTREAMER_FOUND)
        message(STATUS "GStreamer enabled: webcam capture via GStreamer")
        add_compile_definitions(USE_GSTREAMER=1)
        include_directories(${GSTREAMER_INCLUDE_DIRS})
    endif()
else()
    message(STATUS "GStreamer disabled: using V4L2 on Linux (default)")
endif()

# Optional OpenCV support for advanced processing (SmartCapture)
option(QSCAN_ENABLE_OPENCV "Enable OpenCV for advanced image processing (SmartCapture)" OFF)

if(QSCAN_ENABLE_OPENCV)
    # Only require core and imgproc modules (not the full OpenCV suite)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV enabled: SmartCapture will be available")
        add_compile_definitions(HAVE_OPENCV=1)
    endif()
else()
    message(STATUS "OpenCV disabled: SmartCapture will not be available (faster builds)")
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
    
    # Scan backend
    src/scan_scan_manager.cpp
    src/scan_sane_scan_device.cpp
    src/scan_webcam_source.cpp
    
    # GUI
    src/gui_mainwindow.cpp
    src/gui_scan_preview_widget.cpp
    src/gui_page_list_widget.cpp
    src/gui_scanner_selector.cpp
)

# Add SmartCapture processor only if OpenCV is enabled
if(QSCAN_ENABLE_OPENCV)
    list(APPEND SOURCES src/processing_smart_capture_processor.cpp)
endif()

# Header files with Q_OBJECT that need MOC processing
set(HEADERS_WITH_MOC
    inc/document/document.hpp
    inc/scan/scansource.hpp
    inc/scan/sanescandevice.hpp
    inc/scan/webcamsource.hpp
    inc/scan/scanmanager.hpp
    inc/gui/mainwindow.hpp
    inc/gui/scanpreviewwidget.hpp
    inc/gui/pagelistwidget.hpp
    inc/gui/scannerselector.hpp
)

# Create executable
add_executable(qscan ${SOURCES} ${HEADERS_WITH_MOC})

# Link libraries
if(Qt6_FOUND)
    target_link_libraries(qscan
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        ${SANE_LIBRARIES}
    )
else()
    target_link_libraries(qscan
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
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

# Link directories
target_link_directories(qscan PRIVATE ${SANE_LIBRARY_DIRS})

# Installation
install(TARGETS qscan DESTINATION bin)

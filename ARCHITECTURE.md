# QScan Architecture Document

## Overview
QScan is a Qt-based scanning application for Linux that supports both image and document scanning with auto-crop and auto-rotate capabilities. It interfaces with SANE-compatible scanners and V4L2 webcams.

## Design Principles

### Separation of Concerns
The application is divided into distinct layers with clear responsibilities:
- **Scan Backend Layer**: Hardware interface (SANE, V4L2)
- **Processing Layer**: Image manipulation (OpenCV-based)
- **Document Model Layer**: Data management
- **GUI Layer**: User interface

### No Cross-Layer Pollution
- GUI classes do NOT call SANE functions directly
- GUI classes do NOT call OpenCV functions directly
- Backend classes are UI-agnostic
- Processing classes are isolated with no GUI dependencies

## Architecture Layers

### Layer 1: Scan Backend (scan/)

**Purpose**: Interface with scanner hardware

**Components**:
- `ScanSource` - Abstract interface for all scan sources
- `ScanCapabilities` - Describes what a scan source can do
- `SANEScanDevice` - SANE scanner implementation
- `WebcamSource` - V4L2 webcam implementation
- `ScanManager` - Factory and device enumeration

**Key Responsibilities**:
- Device enumeration and initialization
- Scanning operations
- Multi-page handling (ADF support)
- Emit signals for GUI updates (NO direct GUI calls)

**SANE Integration Details**:
- Query dimensions via `SANE_NAME_SCAN_TL_X/Y` and `SANE_NAME_SCAN_BR_X/Y`
- Default scan area: A4 (210×297mm)
- Multi-page: Detect ADF via `SANE_NAME_DOCUMENT_FEEDER` option
- Page completion: `sane_read()` returns `SANE_STATUS_EOF`
- Batch end: `SANE_STATUS_NO_DOCS` when feeder empty

### Layer 2: Image Processing (processing/)

**Purpose**: Image manipulation algorithms

**Two-Tier Architecture**:
The processing layer is designed with two tiers to minimize dependencies:

**Tier 1: Basic Processing (Always Available - Qt-only)**
- `CropProcessor` - Crop images to rectangles (manual or auto-detected)
- `RotateProcessor` - Rotate images by fixed or arbitrary angles
- `BorderDetector` - Detect white borders and skew in scanner output
- `EnhanceProcessor` - Brightness, contrast, auto-levels

**Tier 2: Advanced Processing (Optional - Requires OpenCV)**
- `SmartCaptureProcessor` - Detect documents in photos, perspective correction

**Key Design Decisions**:
1. **Separation of Detection and Action**: 
   - Detection classes (e.g., `BorderDetector`) find what needs to be done
   - Action classes (e.g., `CropProcessor`, `RotateProcessor`) perform the operation
   - GUI decides whether to use auto-detection or manual input

2. **Manual vs. Automatic Operations**:
   - Same processor handles both: `CropProcessor::crop(image, rect)` doesn't care if rect came from user drawing or `BorderDetector`
   - GUI controls the workflow: auto-crop = detect + crop, manual crop = user draws + crop

3. **OpenCV Isolation**:
   - Only `SmartCaptureProcessor` requires OpenCV (for perspective transforms)
   - All other features work with Qt-only implementations
   - Build flag `QSCAN_ENABLE_OPENCV` controls OpenCV inclusion (OFF by default)
   - When disabled: faster builds, smaller AppImage, no heavy dependencies

4. **Capability Flags**:
   - `ImageProcessor::availableCapabilities()` returns which features are available
   - GUI can check capabilities and hide/disable unavailable features
   - Example: SmartCapture button only shown if `HAVE_OPENCV` is defined

**Processing Workflows**:

*Scanner Auto-Crop (Qt-only)*:
```
Scan → BorderDetector::detectContentBounds() → CropProcessor::crop() → Display
```

*Scanner Manual Crop (Qt-only)*:
```
Scan → User draws rectangle → CropProcessor::crop() → Display
```

*Webcam Smart Capture (Requires OpenCV)*:
```
Photo → SmartCaptureProcessor::detectDocument() → GUI overlays polygon
→ User adjusts corners → SmartCaptureProcessor::extractDocument() → Display
```

*Manual Rotate (Qt-only)*:
```
User clicks "Rotate 90°" → RotateProcessor::rotate(image, 90) → Display
```

*Auto-Rotate/Deskew (Qt-only)*:
```
Scan → BorderDetector::detectSkewAngle() → RotateProcessor::rotate(image, angle) → Display
```

**Technology Stack**:
- **Qt (always)**: Basic image operations, transforms, pixel manipulation
- **OpenCV (optional)**: Only for `SmartCaptureProcessor` - perspective transforms, contour detection in complex scenes
  - When enabled: Only links `opencv_core` and `opencv_imgproc` (not full OpenCV suite)
  - Avoids heavy dependencies: GDAL, HDF5, video codecs, ML modules

**Metadata Handling**:
- Use Qt's `QImageWriter::setText()` for basic EXIF
- Store: scan software name, scanner device, scan date

### Layer 3: Document Model (document/)

**Purpose**: Data representation and export

**Components**:
- `ScannedPage` - Single page with image, rotation, crop info
- `Document` - Collection of pages with scan mode
- `DocumentExporter` - Export to JPG/PNG/PDF

**Scan Modes**:
- `IMAGE_MODE` - Single image focus, JPG output
- `DOCUMENT_MODE` - Multi-page support, PDF output

**PDF Export**:
- Initial: Qt's `QPdfWriter` (simple, built-in)
- Future: Can swap to Poppler/PoDoFo if needed
- `DocumentExporter` provides abstraction layer

### Layer 4: GUI (gui/)

**Purpose**: User interface

**Components**:
- `ScannerSelector` - Initial dialog for device/mode selection
- `MainWindow` - Main scanning interface
- `ScanPreviewWidget` - QGraphicsView-based image display
- `PageListWidget` - Thumbnail list for multi-page documents
- `SettingsDialog` - Application preferences

**Layout**:
```
┌─────────────────────────────────────────────────┐
│ [Scan] [Save] [Settings]        Toolbar         │
├──────────────┬──────────────────────────────────┤
│              │                                  │
│  Page List   │   ScanPreviewWidget              │
│  (Document   │   (QGraphicsView)                │
│   mode only) │                                  │
│              │   Initial: A4 placeholder        │
│  ┌────────┐  │   After scan: scaled image       │
│  │ Page 1 │  │                                  │
│  └────────┘  │   Zoom: [Fit] [100%] [+] [-]    │
│  ┌────────┐  │                                  │
│  │ Page 2 │  │                                  │
│  └────────┘  │                                  │
│  [+ Add]     │                                  │
│              │                                  │
├──────────────┴──────────────────────────────────┤
│ Status: Scanner ready | A4 (210×297mm)          │
└─────────────────────────────────────────────────┘
```

**Display Strategy**:
- Placeholder: A4 aspect ratio (1:1.414), light gray with dashed border
- Post-scan: Image scaled to fit with `Qt::KeepAspectRatio`
- Page list only visible in `DOCUMENT_MODE`

## Key Workflows

### Workflow 1: Application Startup
1. `main()` creates `ScanManager`
2. `ScanManager` enumerates SANE devices + V4L2 webcams
3. Show `ScannerSelector` dialog
4. User selects device and mode (Image/Document)
5. Create `MainWindow` with selected source
6. Display A4 placeholder in preview area
7. Status bar shows scanner name and dimensions

### Workflow 2: Single Scan (Image Mode)
1. User clicks "Scan" button
2. `MainWindow` calls `ScanSource::startScan()`
3. Backend emits `pageScanned(QImage)`
4. `MainWindow` optionally applies auto-crop/rotate
5. Updates preview with processed image
6. Backend emits `scanComplete()`
7. User saves as JPG/PNG

### Workflow 3: Multi-Page ADF Scan (Document Mode)
1. User clicks "Scan" button
2. `ScanSource::startScan()` initiated
3. For each page in ADF:
   - Backend emits `pageScanned(QImage, pageNum)`
   - `MainWindow` processes and adds to `Document`
   - `PageListWidget` updates with new thumbnail
4. When ADF empty, backend emits `scanComplete()`
5. User can add more pages manually
6. User saves as multi-page PDF

### Workflow 4: Manual Multi-Page (Document Mode)
1. User clicks "Scan" → one page scanned
2. User clicks "Add Page" in `PageListWidget`
3. Triggers another scan
4. Repeat until complete
5. Save as PDF

## Technical Specifications

### SANE Interface
- **Library**: libsane
- **Dimension Query**: `sane_get_option_descriptor()` with scan area options
- **Default Area**: A4 (210×297mm)
- **ADF Detection**: Check `SANE_NAME_DOCUMENT_FEEDER` option
- **Multi-page Protocol**:
  - Call `sane_start()` for each page
  - `sane_read()` until `SANE_STATUS_EOF` (page done)
  - Continue until `SANE_STATUS_NO_DOCS` (feeder empty)

### V4L2 Interface
- **Library**: Video4Linux2 kernel interface
- **Device Discovery**: Enumerate `/dev/video*`
- **Operation**: Single frame capture (no multi-page)
- **Reason**: QCamera broken in Qt5 for some devices

### Image Processing

**Two-Tier Design**:

**Tier 1 - Basic Processing (Qt-only, always available)**:
- **Library**: Qt (Core, Gui)
- **Auto-Crop**: White border detection via pixel scanning
- **Auto-Rotate/Deskew**: Projection-based skew detection
- **Manual Operations**: Crop to rectangle, rotate by angle
- **Brightness/Contrast**: Direct pixel manipulation

**Tier 2 - Advanced Processing (OpenCV, optional)**:
- **Library**: OpenCV (only `core` + `imgproc` modules)
- **Smart Capture**: Document detection in photos via contour finding
- **Perspective Correction**: `cv::getPerspectiveTransform()` + `cv::warpPerspective()`
- **Build Flag**: `QSCAN_ENABLE_OPENCV=ON` (default: OFF)
- **Isolation**: All OpenCV code in `SmartCaptureProcessor` only

**Why This Design**:
- Fast development builds without OpenCV (5-10x faster AppImage creation)
- Smaller AppImage when OpenCV disabled (~50MB vs ~200MB)
- Core scanning features work without heavy dependencies
- Advanced features available when needed

### Document Export
- **Single Images**: Qt's `QImageWriter` with JPG/PNG
- **Multi-page PDF**: `QPdfWriter` (Qt built-in)
- **Metadata**: `QImageWriter::setText()` for EXIF tags
- **Future**: Can add Poppler backend via `DocumentExporter` abstraction

## Dependencies

### Required Libraries
- Qt5/Qt6 (Core, Gui, Widgets)
- SANE (libsane-dev)

### Optional Libraries
- **OpenCV** (libopencv-dev) - For SmartCapture feature
  - Only requires `opencv_core` and `opencv_imgproc` modules
  - Enable with `-DQSCAN_ENABLE_OPENCV=ON` during cmake
  - Default: OFF (faster builds, smaller AppImage)
- **libexif** - Enhanced metadata support (future)

## Build System

### Dual Build Support
- **qmake**: .pro file for Qt Creator integration
- **CMake**: CMakeLists.txt for modern builds
- Both support Qt5 and Qt6

### Build Helper
- Shell script for Podman-based Qt6 build
- Uses `qt-6.10.1-fedora` container image if available

## Project Structure

```
QScan/
├── ARCHITECTURE.md          # This file
├── CMakeLists.txt          # CMake build
├── QScan.pro               # qmake build
├── build.sh                # Podman build helper
│
├── inc/                    # Header files
│   ├── main.hpp
│   ├── settingsmanager.hpp
│   │
│   ├── scan/               # Scan backend
│   │   ├── scansource.hpp
│   │   ├── scancapabilities.hpp
│   │   ├── sanescandevice.hpp
│   │   ├── webcamsource.hpp
│   │   └── scanmanager.hpp
│   │
│   ├── processing/         # Image processing
│   │   ├── imageprocessor.hpp      # Base interface + capability flags
│   │   ├── cropprocessor.hpp       # Crop to rectangle (Qt-only)
│   │   ├── rotateprocessor.hpp     # Rotate by angle (Qt-only)
│   │   ├── borderdetector.hpp      # Detect borders/skew (Qt-only)
│   │   ├── enhanceprocessor.hpp    # Brightness/contrast (Qt-only)
│   │   └── smartcaptureprocessor.hpp  # Document detection (OpenCV, optional)
│   │
│   ├── document/           # Document model
│   │   ├── scannedpage.hpp
│   │   ├── document.hpp
│   │   └── documentexporter.hpp
│   │
│   └── gui/                # GUI layer
│       ├── scannerselector.hpp
│       ├── mainwindow.hpp
│       ├── scanpreviewwidget.hpp
│       ├── pagelistwidget.hpp
│       └── settingsdialog.hpp
│
└── src/                    # Implementation files
    ├── main.cpp
    ├── core_settingsmanager.cpp
    ├── scan_*.cpp
    ├── processing_*.cpp
    ├── document_*.cpp
    └── gui_*.cpp
```

## Implementation Priority

### Phase 1: Core Backend (FOUNDATION)
- `ScanSource` interface
- `ScanCapabilities` structure
- `SANEScanDevice` basic implementation (single page)
- `ScanManager` device enumeration

### Phase 2: Basic GUI (MINIMAL VIABLE)
- `ScannerSelector` dialog
- `MainWindow` skeleton
- `ScanPreviewWidget` with placeholder and image display
- Single scan workflow end-to-end

### Phase 3: Document Model (DATA LAYER)
- `ScannedPage` class
- `Document` class
- `PageListWidget` for thumbnails
- Multi-page support in GUI

### Phase 4: Image Processing (ENHANCEMENT)
- `ImageProcessor` interface with capability flags ✅
- `CropProcessor` + `RotateProcessor` (Qt-only) ✅
- `BorderDetector` for auto-crop/deskew (Qt-only)
- `SmartCaptureProcessor` stub (OpenCV, optional) ✅
- Integration into scan workflow

### Phase 5: Export (OUTPUT)
- `DocumentExporter` implementation
- QPdfWriter integration
- Save functionality

### Phase 6: Webcam Support (ALTERNATIVE INPUT)
- `WebcamSource` implementation
- V4L2 integration
- Webcam enumeration in `ScanManager`

### Phase 7: Polish (REFINEMENT)
- `SettingsDialog` implementation
- Error handling and validation
- Progress indicators
- Metadata writing
- UI polish and testing

## Configuration

### Settings (via SettingsManager)
- `scanner.last_device` - Last used scanner ID
- `scanner.default_mode` - "image" or "document"
- `paths.save_location` - Default save directory
- `processing.auto_crop` - Auto-crop enabled (bool)
- `processing.auto_rotate` - Auto-rotate enabled (bool)
- `scanner.default_resolution` - Default DPI
- `scanner.default_color_mode` - "Color", "Gray", or "BW"

## License

GPL v3 (consistent with existing project files)

## Future Enhancements

### Possible Additions
1. **Specialized scanners**: Film scanners, specific vendor support
2. **Advanced processing**: De-skew, noise reduction, OCR
3. **Cloud integration**: Direct upload to services
4. **Batch operations**: Multiple documents in queue
5. **Format support**: TIFF, multi-page TIFF
6. **Enhanced PDF**: Text layer, compression options (Poppler)
7. **Scanner profiles**: Save/load scanner configurations
8. **Preview scan**: Low-res preview before full scan

### Architecture Extensibility
- `ScanSource` interface allows new scanner types
- `ImageProcessor` interface allows plugin-style processing
- `DocumentExporter` abstraction allows new export formats
- Settings system supports new configuration options

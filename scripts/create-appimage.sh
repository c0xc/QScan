#!/bin/bash
# Script to create AppImage with bundled dependencies
# This script is designed to run inside the build container
# It bundles Qt6, OpenCV - not SANE (see below)
# The SANE backend is only useful if scanners are already installed
# which implies SANE already being installed on the system.

set -e

# NOTE SANE in AppImage container (sadness):
# If jpeg libs and some others are not excluded from the AppImage build,
# the SANE backend will silently fail to dl_load the driver libs,
# returning an empty list of scanners. More below.
# SANE host setup: dlopen backend <hpaio>: FAILED: /tmp/.mount_QScan-*/usr/lib/libcrypto.so.3: version `OPENSSL_3.3.0' not found (required by /lib64/libssl.so.3)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"
APPDIR="$PROJECT_DIR/AppDir"
BINARY="$BUILD_DIR/qscan"

# If the build disabled the GStreamer webcam backend, prefer using host GStreamer
# for Qt Multimedia to avoid mixing bundled core libs with host plugins.
QSCAN_BUNDLE_GSTREAMER=1
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    if grep -q '^QSCAN_ENABLE_GSTREAMER:BOOL=OFF$' "$BUILD_DIR/CMakeCache.txt"; then
        QSCAN_BUNDLE_GSTREAMER=0
    fi
fi

echo "Creating AppImage for QScan..."
echo "Project directory: $PROJECT_DIR"
echo "Build directory: $BUILD_DIR"
echo "AppDir: $APPDIR"

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found at $BINARY"
    echo "Please build the project first."
    exit 1
fi

# Clean up old AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR"

# Create AppDir structure
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy binary
echo "Copying binary..."
cp "$BINARY" "$APPDIR/usr/bin/"

# Function to copy library and its dependencies recursively
copy_deps() {
    local lib=$1
    local target_dir=$2
    
    # Skip if library doesn't exist
    if [ ! -f "$lib" ]; then
        return
    fi
    
    # Get the library name
    local libname=$(basename "$lib")

    # Do not bundle OpenGL/GLX driver-facing libraries. They are tightly coupled
    # to the host GPU driver stack and bundling them often breaks GLX/EGL.
    case "$libname" in
        libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*)
            return
            ;;
    esac

    # Do not bundle OpenSSL or core image codec libs.
    # Host SANE backends are loaded at runtime and must bind against the host's
    # libssl/libcrypto/libjpeg/libtiff stack. Bundling these libraries can
    # pre-load incompatible versions and prevent backends like hpaio/airscan
    # from loading.
    # Example failures observed when these were bundled in the AppImage:
    # SANE host setup: dlopen backend <hpaio>: FAILED: /tmp/.mount_QScan-*/usr/lib/libcrypto.so.3: version `OPENSSL_3.3.0' not found (required by /lib64/libssl.so.3)
    # SANE host setup: dlopen backend <airscan>: FAILED: /lib64/libtiff.so.6: undefined symbol: jpeg12_read_raw_data, version LIBJPEG_6.2
    case "$libname" in
        libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
            return
            ;;
    esac

    if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
        case "$libname" in
            libgst*.so*|libgstreamer-1.0.so* )
                return
                ;;
            # If we are intentionally using host GStreamer (no-gst build), we must
            # also use host GLib to avoid symbol/ABI mismatches.
            libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                return
                ;;
        esac
    fi
    
    # Skip if already copied
    if [ -f "$target_dir/$libname" ]; then
        return
    fi
    
    # Copy the library
    cp "$lib" "$target_dir/"
    
    # Get dependencies using ldd
    ldd "$lib" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
        if [ -n "$dep" ] && [ -f "$dep" ]; then
            local depname=$(basename "$dep")
            
            # Skip system libraries that should be present on all systems
            case "$depname" in
                libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*|libstdc++.so*)
                    continue
                    ;;
            esac
            
            # Skip ld-linux
            if [[ "$depname" == ld-linux* ]]; then
                continue
            fi

            # Prefer using host SANE to match the host's installed backends/config
            case "$depname" in
                libsane.so*|libsane-*.so*)
                    continue
                    ;;
            esac

            # Same rationale as above: keep OpenSSL and core image codec libs host-side
            case "$depname" in
                libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
                    continue
                    ;;
            esac
            
            # Recursively copy dependencies
            copy_deps "$dep" "$target_dir"
        fi
    done
}

# Copy only the dependencies of a file (not the file itself)
copy_deps_only() {
    local file=$1
    local target_dir=$2

    if [ ! -f "$file" ]; then
        return
    fi

    ldd "$file" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
        if [ -n "$dep" ] && [ -f "$dep" ]; then
            local depname=$(basename "$dep")
            case "$depname" in
                libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*|libstdc++.so*)
                    continue
                    ;;
            esac
            if [[ "$depname" == ld-linux* ]]; then
                continue
            fi

            case "$depname" in
                libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*)
                    continue
                    ;;
            esac

            case "$depname" in
                libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
                    continue
                    ;;
            esac
            if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
                case "$depname" in
                    libgst*.so*|libgstreamer-1.0.so* )
                        continue
                        ;;
                    libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                        continue
                        ;;
                esac
            fi
            copy_deps "$dep" "$target_dir"
        fi
    done
}

echo "Bundling dependencies..."

# Get all dependencies of the binary
echo "  - Analyzing binary dependencies..."
ldd "$BINARY" | grep "=>" | awk '{print $3}' | while read -r lib; do
    if [ -n "$lib" ] && [ -f "$lib" ]; then
        libname=$(basename "$lib")
        
        # Skip basic system libraries
        case "$libname" in
            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*)
                echo "    Skipping system library: $libname"
                continue
                ;;
        esac

        # Prefer using host SANE to match the host's installed backends/config.
        # Bundling libsane can cause ABI mismatches with distro/vendor backends.
        case "$libname" in
            libsane.so*|libsane-*.so*)
                echo "    Skipping SANE library (use host): $libname"
                continue
                ;;
        esac

        # If the build disabled our GStreamer webcam backend, Qt Multimedia will still
        # typically use the host's GStreamer plugin on Linux. In that case we must
        # avoid bundling GLib, otherwise host GStreamer may bind against our bundled
        # GLib and crash/abort due to symbol mismatches.
        if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
            case "$libname" in
                libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                    echo "    Skipping GLib library (use host): $libname"
                    continue
                    ;;
            esac
        fi
        
        # Skip ld-linux
        if [[ "$libname" == ld-linux* ]]; then
            echo "    Skipping dynamic linker: $libname"
            continue
        fi

        # Do not bundle OpenGL/GLX driver-facing libraries. Use host.
        case "$libname" in
            libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*)
                echo "    Skipping OpenGL library (use host): $libname"
                continue
                ;;
        esac

        # Avoid bundling OpenSSL and core image codec libs to prevent runtime
        # conflicts with host-loaded SANE backends.
        case "$libname" in
            libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
                echo "    Skipping library (use host): $libname"
                continue
                ;;
        esac
        
        echo "    Bundling: $libname"
        copy_deps "$lib" "$APPDIR/usr/lib"
    fi
done

# Check if OpenCV is linked (only bundle if actually used)
if ldd "$BINARY" | grep -q "libopencv"; then
    echo "  - OpenCV detected, bundling OpenCV modules..."
    for opencv_lib in /usr/lib64/libopencv_*.so* /usr/lib/x86_64-linux-gnu/libopencv_*.so*; do
        if [ -f "$opencv_lib" ]; then
            libname=$(basename "$opencv_lib")
            if [ ! -f "$APPDIR/usr/lib/$libname" ]; then
                echo "    Bundling OpenCV module: $libname"
                cp "$opencv_lib" "$APPDIR/usr/lib/" 2>/dev/null || true
            fi
        fi
    done
else
    echo "  - OpenCV not linked, skipping OpenCV modules"
fi

# Bundle Qt plugins
echo "  - Looking for Qt plugins..."
QT_PLUGIN_PATHS="/usr/local/plugins /usr/lib64/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins /usr/local/lib/qt6/plugins"

for qt_path in $QT_PLUGIN_PATHS; do
    if [ -d "$qt_path" ]; then
        echo "    Found Qt plugins at: $qt_path"
        mkdir -p "$APPDIR/usr/plugins"
        cp -r "$qt_path/platforms" "$APPDIR/usr/plugins/" 2>/dev/null || true
        cp -r "$qt_path/imageformats" "$APPDIR/usr/plugins/" 2>/dev/null || true
        cp -r "$qt_path/iconengines" "$APPDIR/usr/plugins/" 2>/dev/null || true
        cp -r "$qt_path/xcbglintegrations" "$APPDIR/usr/plugins/" 2>/dev/null || true
        cp -r "$qt_path/multimedia" "$APPDIR/usr/plugins/" 2>/dev/null || true
        
        # Bundle dependencies of Qt plugins by analyzing original plugin files
        # This matters for plugins loaded via dlopen() (not visible in the main binary's ldd)
        # In particular, SVG icons require the qsvg icon engine/imageformat plugin and libQtSvg
        echo "    Bundling Qt plugin dependencies..."
        for plugin_src in \
            "$qt_path/platforms"/*.so \
            "$qt_path/xcbglintegrations"/*.so \
            "$qt_path/iconengines"/*.so \
            "$qt_path/imageformats"/*.so; do
            if [ -f "$plugin_src" ]; then
                ldd "$plugin_src" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
                    if [ -n "$dep" ] && [ -f "$dep" ]; then
                        depname=$(basename "$dep")
                        # Skip basic system libraries
                        case "$depname" in
                            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*)
                                continue ;;
                        esac
                        if [[ "$depname" == ld-linux* ]]; then
                            continue
                        fi

                        if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
                            case "$depname" in
                                libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                                    continue
                                    ;;
                            esac
                        fi

                        case "$depname" in
                            libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*)
                                continue
                                ;;
                        esac

                        # Keep OpenSSL/JPEG/TIFF host-side to avoid breaking host SANE backends
                        case "$depname" in
                            libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
                                continue
                                ;;
                        esac
                        # Bundle if not already present
                        if [ ! -f "$APPDIR/usr/lib/$depname" ]; then
                            echo "      Bundling plugin dep: $depname"
                            copy_deps "$dep" "$APPDIR/usr/lib"
                        fi
                    fi
                done
            fi
        done

        if [ -d "$qt_path/multimedia" ]; then
            find "$qt_path/multimedia" -type f -name "*.so" | while read -r plugin_src; do
                if [ -f "$plugin_src" ]; then
                    ldd "$plugin_src" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
                        if [ -n "$dep" ] && [ -f "$dep" ]; then
                            depname=$(basename "$dep")
                            # Skip basic system libraries
                            case "$depname" in
                                libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*)
                                    continue ;;
                            esac
                            if [[ "$depname" == ld-linux* ]]; then
                                continue
                            fi

                            if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
                                case "$depname" in
                                    libgst*.so*|libgstreamer-1.0.so* )
                                        continue
                                        ;;
                                    libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                                        continue
                                        ;;
                                esac
                            fi

                            # Keep OpenSSL/JPEG/TIFF host-side to avoid breaking host SANE backends
                            case "$depname" in
                                libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
                                    continue
                                    ;;
                            esac
                            # Bundle if not already present
                            if [ ! -f "$APPDIR/usr/lib/$depname" ]; then
                                echo "      Bundling plugin dep: $depname"
                                copy_deps "$dep" "$APPDIR/usr/lib"
                            fi
                        fi
                    done
                fi
            done
        fi
        break
    fi
done

# Hard fail-safe: do not ship OpenSSL/JPEG/TIFF into AppImage
# TODO this is supposed to "fix" SANE support but breaks the AppImage (e.g., in Ubuntu in distrobox)
# Why Distrobox? Because some manufacturers only provide a deb package with their drivers
# and on a RedHat-based distro, Distrobox is a simple workaround (which also allows GUI programs like this one to run) 
rm -f "$APPDIR/usr/lib/libcrypto.so"* "$APPDIR/usr/lib/libssl.so"* "$APPDIR/usr/lib/libjpeg.so"* "$APPDIR/usr/lib/libtiff.so"* 2>/dev/null || true


if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 1 ]; then
    # Bundle minimal GStreamer runtime plugins needed for webcam enumeration/capture.
    # Keep minimal to avoid shipping codecs
    echo "  - Looking for GStreamer plugins..."
    GST_PLUGIN_DIRS="/usr/lib64/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    # Needed for v4l2src/appsink/videoconvert
    GST_MIN_PLUGINS="libgstapp.so libgstvideoconvert.so libgstvideo4linux2.so"

    for gst_dir in $GST_PLUGIN_DIRS; do
        if [ -d "$gst_dir" ]; then
            echo "    Found GStreamer plugins at: $gst_dir"
            mkdir -p "$APPDIR/usr/lib/gstreamer-1.0"

            for plugin in $GST_MIN_PLUGINS; do
                if [ -f "$gst_dir/$plugin" ]; then
                    echo "      Bundling GStreamer plugin: $plugin"
                    cp "$gst_dir/$plugin" "$APPDIR/usr/lib/gstreamer-1.0/" 2>/dev/null || true
                    copy_deps_only "$gst_dir/$plugin" "$APPDIR/usr/lib"
                else
                    echo "      WARNING: GStreamer plugin not found: $gst_dir/$plugin"
                fi
            done
            break
        fi
    done

    # Bundle gst-plugin-scanner helper if present (used for plugin discovery)
    echo "  - Looking for gst-plugin-scanner..."
    GST_SCANNER_CANDIDATES="/usr/libexec/gstreamer-1.0/gst-plugin-scanner /usr/lib/x86_64-linux-gnu/gstreamer-1.0/gst-plugin-scanner /usr/lib/gstreamer-1.0/gst-plugin-scanner"
    for scanner in $GST_SCANNER_CANDIDATES; do
        if [ -f "$scanner" ]; then
            echo "    Found gst-plugin-scanner at: $scanner"
            mkdir -p "$APPDIR/usr/libexec/gstreamer-1.0"
            cp "$scanner" "$APPDIR/usr/libexec/gstreamer-1.0/" 2>/dev/null || true
            copy_deps_only "$scanner" "$APPDIR/usr/lib"
            break
        fi
    done
fi

# Create desktop file
echo "Creating desktop file..."
cat > "$APPDIR/usr/share/applications/qscan.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=QScan
Comment=Document Scanner Application
Exec=qscan
Icon=qscan
Categories=Office;Scanning;
Terminal=false
StartupWMClass=QScan
EOF

echo "Creating icon..."

ICON_SRC=""
ICON_SRC_CANDIDATES=(
    "$PROJECT_DIR/resources/icons/QScan_"*.png
    "$PROJECT_DIR/resources/icons/app.png"
    "$PROJECT_DIR/QScan_"*.png
)
for c in "${ICON_SRC_CANDIDATES[@]}"; do
    if [ -f "$c" ]; then
        ICON_SRC="$c"
        break
    fi
done

if [ -n "$ICON_SRC" ]; then
    cp -f "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/qscan.png"
else
    # Fallback: create a minimal valid PNG file (1x1 transparent pixel)
    printf '\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0a\x49\x44\x41\x54\x78\x9c\x63\x00\x01\x00\x00\x05\x00\x01\x0d\x0a\x2d\xb4\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82' > "$APPDIR/usr/share/icons/hicolor/256x256/apps/qscan.png"
fi

# Copy desktop file and icon to AppDir root
cp "$APPDIR/usr/share/applications/qscan.desktop" "$APPDIR/"
ln -sf "usr/share/icons/hicolor/256x256/apps/qscan.png" "$APPDIR/qscan.png"
ln -sf "usr/share/icons/hicolor/256x256/apps/qscan.png" "$APPDIR/.DirIcon"

# Create AppRun script
echo "Creating AppRun launcher..."
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
# AppRun script for QScan

# Get the directory where this AppRun resides
APPDIR=$(dirname "$(readlink -f "${0}")")
export APPDIR

# Set up library paths
export LD_LIBRARY_PATH="$APPDIR/usr/lib:${LD_LIBRARY_PATH}"

# Set up Qt plugin path
export QT_PLUGIN_PATH="$APPDIR/usr/plugins:${QT_PLUGIN_PATH}"

# Avoid GLX initialization issues on some driver stacks when running from AppImage.
export QT_XCB_GL_INTEGRATION=none

# Use host SANE configuration/backends (do not override SANE_CONFIG_DIR).
export QSCAN_SANE_SANITIZE_LD_LIBRARY_PATH=1
EOF

if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 1 ]; then
    cat >> "$APPDIR/AppRun" << 'EOF'

# Use bundled minimal GStreamer plugins. Keep the host system plugin path available
# as a fallback (some distros split device providers into separate plugins).
export GST_PLUGIN_PATH="$APPDIR/usr/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER="$APPDIR/usr/libexec/gstreamer-1.0/gst-plugin-scanner"
# Keep registry in a writable location
export GST_REGISTRY="${XDG_CACHE_HOME:-$HOME/.cache}/qscan/gstreamer.registry.bin"
EOF
fi

cat >> "$APPDIR/AppRun" << 'EOF'

# Disable Qt's automatic scaling detection
export QT_AUTO_SCREEN_SCALE_FACTOR=0

# Run the application
exec "$APPDIR/usr/bin/qscan" "$@"
EOF

chmod +x "$APPDIR/AppRun"

echo "AppDir structure created successfully!"
echo "Contents of AppDir:"
ls -lR "$APPDIR" | head -50

# Create the AppImage
echo ""
echo "Creating AppImage with appimagetool..."
export ARCH=x86_64

if command -v appimagetool &> /dev/null; then
    appimagetool "$APPDIR" "$PROJECT_DIR/QScan-x86_64.AppImage"
elif [ -f "/opt/appimagetool/AppRun" ]; then
    /opt/appimagetool/AppRun "$APPDIR" "$PROJECT_DIR/QScan-x86_64.AppImage"
else
    echo "Error: appimagetool not found. Cannot create AppImage."
    echo "AppDir is ready at: $APPDIR"
    exit 1
fi

if [ -f "$PROJECT_DIR/QScan-x86_64.AppImage" ]; then
    echo ""
    echo "========================================"
    echo "AppImage created successfully!"
    echo "Location: $PROJECT_DIR/QScan-x86_64.AppImage"
    echo "Size: $(du -h "$PROJECT_DIR/QScan-x86_64.AppImage" | cut -f1)"
    echo "========================================"
    
    # Make it executable
    chmod +x "$PROJECT_DIR/QScan-x86_64.AppImage"
else
    echo ""
    echo "Error: AppImage was not created"
    exit 1
fi

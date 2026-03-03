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

# AppImage runtime media backend policy
# Default to FFmpeg inside AppImage because this is the backend that is stable
# in the non-AppImage Qt6 build and avoids the observed webcam freeze path seen
# with Qt Multimedia + GStreamer inside the AppImage.
# Set QSCAN_APPIMAGE_PREFER_FFMPEG=0 to disable this preference.
QSCAN_APPIMAGE_PREFER_FFMPEG="${QSCAN_APPIMAGE_PREFER_FFMPEG:-1}"

# Qt Multimedia plugin policy depends on webcam backend mode:
# - QtCamera mode (QSCAN_ENABLE_GSTREAMER=OFF): default skip Qt GStreamer multimedia
#   plugin because AppImage webcam freeze was observed on that path
# - GStreamer backend mode (QSCAN_ENABLE_GSTREAMER=ON): default include it
# Override explicitly with QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN=0/1.
QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN_DEFAULT=1
if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
    QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN_DEFAULT=0
fi
QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN="${QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN:-$QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN_DEFAULT}"

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

# Return 0 with a reason string when a library should stay host-side
skip_reason_for_lib() {
    local libname=$1

    #Keep glibc/libstdc++/toolchain runtime on host
    case "$libname" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libgcc_s.so*|libstdc++.so*|ld-linux*)
            echo "core runtime"
            return 0
            ;;
    esac

    #Keep SANE host-side so distro/vendor backends match the host libs
    case "$libname" in
        libsane.so*|libsane-*.so*)
            echo "host SANE/backend ABI coupling"
            return 0
            ;;
    esac

    #Do not bundle GL dispatch stack
    #These are resolved through the host GPU driver stack; bundling leads to driver mismatch
    case "$libname" in
        libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*)
            echo "host GPU driver coupling"
            return 0
            ;;
    esac

    #Do not bundle OpenSSL/JPEG/TIFF
    #If these are bundled, host SANE backends loaded with dlopen() can bind against
    #the AppImage copy first (due to LD_LIBRARY_PATH), then fail with unresolved
    #versioned symbols because backend and transitive host deps were built against a
    #different distro toolchain set.
    #Observed failures:
    # - hpaio backend: OPENSSL_3.3.0 not found via AppImage libcrypto/libssl
    # - airscan backend: LIBJPEG_6.2 symbol mismatch via libtiff/libjpeg chain
    case "$libname" in
        libcrypto.so*|libssl.so*|libjpeg.so*|libtiff.so*)
            echo "host SANE openssl/jpeg/tiff ABI coupling"
            return 0
            ;;
    esac

    #Do not bundle PCRE2
    #Host GLib may load it through LD_LIBRARY_PATH and then reject symbol/version info
    case "$libname" in
        libpcre2-8.so*)
            echo "host GLib/PCRE2 ABI coupling"
            return 0
            ;;
    esac

    #When the build disables the in-app GStreamer backend, avoid bundling any gst/glib
    #runtime set. Qt Multimedia then stays aligned with the host plugin stack instead
    #of mixing host plugins with bundled core libs.
    if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
        case "$libname" in
            libgst*.so*|libgstreamer-1.0.so*)
                echo "host GStreamer consistency"
                return 0
                ;;
            libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libgthread-2.0.so* )
                echo "host GLib consistency"
                return 0
                ;;
        esac
    fi

    return 1
}

# Function to copy library and its dependencies recursively
copy_deps() {
    local lib=$1
    local target_dir=$2
    local libname reason dep depname

    if [ ! -f "$lib" ]; then
        return
    fi

    libname=$(basename "$lib")
    if reason=$(skip_reason_for_lib "$libname"); then
        return
    fi

    if [ -f "$target_dir/$libname" ]; then
        return
    fi

    cp "$lib" "$target_dir/"

    ldd "$lib" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
        if [ -n "$dep" ] && [ -f "$dep" ]; then
            depname=$(basename "$dep")
            if reason=$(skip_reason_for_lib "$depname"); then
                continue
            fi
            copy_deps "$dep" "$target_dir"
        fi
    done
}

# Copy only the dependencies of a file (not the file itself)
copy_deps_only() {
    local file=$1
    local target_dir=$2
    local dep depname reason

    if [ ! -f "$file" ]; then
        return
    fi

    ldd "$file" 2>/dev/null | grep "=>" | awk '{print $3}' | while read -r dep; do
        if [ -n "$dep" ] && [ -f "$dep" ]; then
            depname=$(basename "$dep")
            if reason=$(skip_reason_for_lib "$depname"); then
                continue
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
        if reason=$(skip_reason_for_lib "$libname"); then
            echo "    Skipping ($reason): $libname"
            continue
        fi
        
        echo "    Bundling: $libname"
        copy_deps "$lib" "$APPDIR/usr/lib"
    fi
done

# Bundle fonts if present
# Some Qt builds rely on a Qt-internal font directory and do not use fontconfig
echo "  - Bundling fonts (if available)..."
mkdir -p "$APPDIR/usr/lib/fonts"
FONT_CANDIDATES=(
    "$PROJECT_DIR/resources/Roboto-Regular.ttf"
    "$PROJECT_DIR/resources/Inconsolata.ttf"
)
for font in "${FONT_CANDIDATES[@]}"; do
    if [ -f "$font" ]; then
        cp -f "$font" "$APPDIR/usr/lib/fonts/"
    fi
done

# Check if OpenCV is linked (only bundle if actually used)
if ldd "$BINARY" | grep -q "libopencv"; then
    echo "  - OpenCV detected, bundling OpenCV modules..."
    # NOTE: This currently grabs most libopencv_* modules from the image
    # If OpenCV usage/modules change, consider restricting this to the actually needed libs
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
        
        # Bundle Qt Multimedia plugins
        # At least one multimedia backend plugin MUST end up in the AppImage,
        # otherwise Qt returns 0 cameras and the app has no webcam support.
        # The Qt build in this container only has the GStreamer multimedia plugin
        # (FFmpeg-devel is not installed, so Qt did not build libffmpegmediaplugin).
        # Bundle whatever is available. Never leave the directory empty.
        mkdir -p "$APPDIR/usr/plugins/multimedia"
        MULTIMEDIA_PLUGIN_BUNDLED=0
        if [ -f "$qt_path/multimedia/libffmpegmediaplugin.so" ]; then
            echo "    Bundling FFmpeg multimedia plugin"
            cp "$qt_path/multimedia/libffmpegmediaplugin.so" "$APPDIR/usr/plugins/multimedia/"
            MULTIMEDIA_PLUGIN_BUNDLED=1
        fi
        if [ -f "$qt_path/multimedia/libgstreamermediaplugin.so" ]; then
            if [ "$QSCAN_APPIMAGE_INCLUDE_GSTREAMER_PLUGIN" = "1" ] || [ "$MULTIMEDIA_PLUGIN_BUNDLED" -eq 0 ]; then
                echo "    Bundling GStreamer multimedia plugin"
                cp "$qt_path/multimedia/libgstreamermediaplugin.so" "$APPDIR/usr/plugins/multimedia/"
                MULTIMEDIA_PLUGIN_BUNDLED=1
            else
                echo "    Skipping GStreamer multimedia plugin (FFmpeg already bundled)"
            fi
        fi
        if [ "$MULTIMEDIA_PLUGIN_BUNDLED" -eq 0 ]; then
            echo "    WARNING: No Qt Multimedia backend plugin found — webcam support will be broken!"
        fi
        
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
                        if reason=$(skip_reason_for_lib "$depname"); then
                            continue
                        fi
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
                            if reason=$(skip_reason_for_lib "$depname"); then
                                continue
                            fi
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

# Hard fail-safe: remove libs that must stay host-side for SANE/backend compatibility.
# This is intentionally redundant with skip rules above because future edits can add
# new copy paths. If these files remain in AppDir, runtime failures are subtle:
# scanner backend dlopen() fails, scanner list can become empty, and users may not
# get a clear UI error about the true ABI mismatch root cause.
rm -f "$APPDIR/usr/lib/libcrypto.so"* "$APPDIR/usr/lib/libssl.so"* "$APPDIR/usr/lib/libjpeg.so"* "$APPDIR/usr/lib/libtiff.so"* 2>/dev/null || true

# Hard fail-safe: remove libpcre2 to avoid host GLib version/symbol conflict
rm -f "$APPDIR/usr/lib/libpcre2-8.so"* 2>/dev/null || true

# Bundle the basic font stack explicitly
# Qt may dlopen() fontconfig at runtime, so it might not show up in ldd output
# If we ship some low-level deps (e.g. libexpat/libz) but rely on host fontconfig,
# mismatches can result in "tofu" rectangles instead of text
echo "  - Bundling font stack (fontconfig/freetype/harfbuzz)..."
FONT_LIB_CANDIDATES=(
    /usr/lib64/libfontconfig.so*
    /usr/lib/x86_64-linux-gnu/libfontconfig.so*
    /usr/lib64/libfreetype.so*
    /usr/lib/x86_64-linux-gnu/libfreetype.so*
    /usr/lib64/libharfbuzz.so*
    /usr/lib/x86_64-linux-gnu/libharfbuzz.so*
)
for font_lib in "${FONT_LIB_CANDIDATES[@]}"; do
    for candidate in $font_lib; do
        if [ -f "$candidate" ]; then
            copy_deps "$candidate" "$APPDIR/usr/lib"
        fi
    done
done


if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 1 ]; then
    # Bundle minimal GStreamer runtime plugins needed for webcam enumeration/capture.
    # Keep minimal to avoid shipping codecs
    echo "  - Looking for GStreamer plugins..."
    GST_PLUGIN_DIRS="/usr/lib64/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    # Minimal set of plugins for the webcam pipeline:
    #   v4l2src ! videoconvert ! appsink
    #
    # Why bundle libgstvideoconvertscale.so for videoconvert:
    # Some distros register the videoconvert element from the "videoconvertscale"
    # plugin module (libgstvideoconvertscale.so), not from libgstvideoconvert.so.
    # For example, in the Fedora build container, `gst-inspect-1.0 videoconvert`
    # reports Plugin Name: videoconvertscale and Filename: libgstvideoconvertscale.so.
    # If we only bundle libgstvideoconvert.so, the AppImage can fail at runtime with
    # "no such element: videoconvert" even though our pipeline uses videoconvert
    # explicitly. Bundling both keeps the set small while staying cross-distro.
    GST_MIN_PLUGINS="libgstapp.so libgstvideoconvert.so libgstvideoconvertscale.so libgstvideo4linux2.so"

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

# Make the bundled desktop entry discoverable for xdg-desktop-portal
# Otherwise Qt may fail to register the app ID with the portal:
# QDBusError("org.freedesktop.portal.Error.Failed", "Could not register app ID: App info not found for 'qscan'")
if [ -z "${XDG_DATA_DIRS+x}" ]; then
    export XDG_DATA_DIRS="$APPDIR/usr/share:/usr/local/share:/usr/share"
else
    export XDG_DATA_DIRS="$APPDIR/usr/share:$XDG_DATA_DIRS"
fi

# Keep native system fonts by default
# If needed for troubleshooting/tofu, force using fonts bundled under $APPDIR/usr/lib/fonts
if [ -z "${QT_QPA_FONTDIR+x}" ] && [ "${QSCAN_FORCE_BUNDLED_FONTS:-0}" = "1" ]; then
    export QT_QPA_FONTDIR="$APPDIR/usr/lib/fonts"
fi

# DO NOT set QT_XCB_GL_INTEGRATION=none - we tried it and failed miserably.
# Setting it to 'none' completely disables OpenGL/EGL integration, which breaks
# Qt Multimedia's video frame conversion pipeline. Observed failures:
# - "qt.multimedia.gstreamer: Using Qt multimedia with GStreamer version: ..."
# - "QGstElement::getPipeline failed for element: videoConvert"
# - "QRhi* ... No RHI backend. Using CPU conversion."
# - Complete freeze when accessing webcam (DMA-BUF video buffer mapping deadlocks
#   without GL context, blocking both GStreamer thread and main event loop)
# The AppImage already excludes GL dispatch libs (libGLX/libEGL/libOpenGL) so the
# host GPU driver stack is used. Let Qt auto-detect the best GL integration method.

# Use host SANE configuration/backends (do not override SANE_CONFIG_DIR).
export QSCAN_SANE_SANITIZE_LD_LIBRARY_PATH=1
EOF

if [ "$QSCAN_BUNDLE_GSTREAMER" -eq 0 ]; then
    cat >> "$APPDIR/AppRun" << 'EOF'

# QtCamera AppImage mode only: prefer FFmpeg backend to avoid the Qt Multimedia
# GStreamer path that has frozen webcam startup in AppImage packaging.
# Related runtime log lines during the freeze path include:
# - "qt.multimedia.gstreamer: Using Qt multimedia with GStreamer version: ..."
# - "QGstElement::getPipeline failed for element: videoConvert"
# - "QRhi* ... No RHI backend. Using CPU conversion."
# Do not apply this in GStreamer-backend builds, where GStreamer is intentional.
if [ "${QSCAN_APPIMAGE_PREFER_FFMPEG:-1}" = "1" ] && [ -f "$APPDIR/usr/plugins/multimedia/libffmpegmediaplugin.so" ]; then
    export QT_MEDIA_BACKEND=ffmpeg
fi
EOF
else
    cat >> "$APPDIR/AppRun" << 'EOF'

# GStreamer-backend build mode: do not force QT_MEDIA_BACKEND.
# Leave Qt backend selection untouched because this build intentionally uses GStreamer.
EOF
fi

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

# Optional non-blocking runtime preflight
# It helps classify library/plugin conflicts early without hard-failing launch
if [ "${QSCAN_RUN_PREFLIGHT:-1}" = "1" ] && [ -z "${QSCAN_PREFLIGHT_RUNNING+x}" ]; then
    export QSCAN_PREFLIGHT_RUNNING=1
    PREFLIGHT_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/qscan"
    mkdir -p "$PREFLIGHT_DIR"
    "$APPDIR/usr/bin/qscan" --self-check --quick >> "$PREFLIGHT_DIR/preflight.log" 2>&1 || true
    unset QSCAN_PREFLIGHT_RUNNING
fi

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

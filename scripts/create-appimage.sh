#!/bin/bash
# Script to create AppImage with bundled dependencies
# This script is designed to run inside the container environment
# It ensures the app runs on systems without OpenCV, Qt6, or SANE installed

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
APPDIR="$PROJECT_DIR/AppDir"
BINARY="$BUILD_DIR/qscan"

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
            
            # Recursively copy dependencies
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
        
        # Skip ld-linux
        if [[ "$libname" == ld-linux* ]]; then
            echo "    Skipping dynamic linker: $libname"
            continue
        fi
        
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
    echo "  - OpenCV not linked, skipping OpenCV modules (faster build!)"
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
        
        # Bundle dependencies of Qt plugins by analyzing original plugin files
        echo "    Bundling Qt plugin dependencies..."
        for plugin_src in "$qt_path/platforms"/*.so "$qt_path/xcbglintegrations"/*.so; do
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
                        # Bundle if not already present
                        if [ ! -f "$APPDIR/usr/lib/$depname" ]; then
                            echo "      Bundling plugin dep: $depname"
                            copy_deps "$dep" "$APPDIR/usr/lib"
                        fi
                    fi
                done
            fi
        done
        break
    fi
done

# Bundle SANE backends if they exist
echo "  - Looking for SANE backends..."
for sane_path in /usr/lib64/sane /usr/lib/x86_64-linux-gnu/sane; do
    if [ -d "$sane_path" ]; then
        echo "    Found SANE backends at: $sane_path"
        mkdir -p "$APPDIR/usr/lib/sane"
        cp -r "$sane_path"/* "$APPDIR/usr/lib/sane/" 2>/dev/null || true
        break
    fi
done

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
EOF

# Create a simple icon placeholder (PNG header for 1x1 transparent pixel)
echo "Creating icon placeholder..."
# Create a minimal valid PNG file
printf '\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0a\x49\x44\x41\x54\x78\x9c\x63\x00\x01\x00\x00\x05\x00\x01\x0d\x0a\x2d\xb4\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82' > "$APPDIR/usr/share/icons/hicolor/256x256/apps/qscan.png"

# Copy desktop file and icon to AppDir root
cp "$APPDIR/usr/share/applications/qscan.desktop" "$APPDIR/"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/qscan.png" "$APPDIR/"
ln -sf "usr/share/icons/hicolor/256x256/apps/qscan.png" "$APPDIR/.DirIcon"

# Create AppRun script
echo "Creating AppRun launcher..."
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
# AppRun script for QScan

# Get the directory where this AppRun resides
APPDIR=$(dirname "$(readlink -f "${0}")")

# Set up library paths
export LD_LIBRARY_PATH="$APPDIR/usr/lib:${LD_LIBRARY_PATH}"

# Set up Qt plugin path
export QT_PLUGIN_PATH="$APPDIR/usr/plugins:${QT_PLUGIN_PATH}"

# Set up SANE backend path
export SANE_CONFIG_DIR="$APPDIR/usr/lib/sane"

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

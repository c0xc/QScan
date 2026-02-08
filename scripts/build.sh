#!/bin/bash
# Build script for QScan using Podman container with Qt 6
# Extended to create AppImage with bundled dependencies
#
# Usage: ./scripts/build.sh [--rebuild-container]
#   --rebuild-container: Force rebuild of the container image

set -e  # Exit on error

# Base container image name (Qt environment)
BASE_IMAGE="qt-6.4-fedora-36"
BASE_IMAGE="qt-6.10.1-fedora"

# Build image name (with QScan dependencies)
BUILD_IMAGE="qscan-fedora-36"

# AppImage creation flag
CREATE_APPIMAGE="${CREATE_APPIMAGE:-true}"

# Parse command line arguments
REBUILD_CONTAINER=false
for arg in "$@"; do
    case $arg in
        --rebuild-container)
            REBUILD_CONTAINER=true
            shift
            ;;
    esac
done

# Check if podman is available
if ! command -v podman &> /dev/null; then
    echo "Error: podman is not installed"
    exit 1
fi

# Check if the base Qt container image exists
if ! podman image exists "$BASE_IMAGE"; then
    echo "Warning: Base container image '$BASE_IMAGE' not found"
    echo "Falling back to local build..."
    
    # Try local build with CMake
    if command -v cmake &> /dev/null; then
        echo "Building with CMake..."
        mkdir -p build
        cd build
        cmake ..
        make -j$(nproc)
        cd ..
        echo "Build complete. Binary is in build/qscan"
        
        # Create AppImage if requested
        if [ "$CREATE_APPIMAGE" = "true" ]; then
            echo "Creating AppImage..."
            bash "$(dirname "$0")/create-appimage.sh"
        fi
    elif command -v qmake &> /dev/null; then
        echo "Building with qmake..."
        mkdir -p build
        cd build
        qmake ..
        make -j$(nproc)
        cd ..
        echo "Build complete. Binary is in build/qscan"
        
        # Create AppImage if requested
        if [ "$CREATE_APPIMAGE" = "true" ]; then
            echo "Creating AppImage..."
            bash "$(dirname "$0")/create-appimage.sh"
        fi
    else
        echo "Error: Neither cmake nor qmake found"
        exit 1
    fi
    exit 0
fi

# Check if we need to rebuild the container image
if [ "$REBUILD_CONTAINER" = true ]; then
    echo "Forcing rebuild of container image '$BUILD_IMAGE'..."
    podman rmi -f "$BUILD_IMAGE" 2>/dev/null || true
fi

# Check if build image exists, create it if not
if ! podman image exists "$BUILD_IMAGE"; then
    echo "Build image '$BUILD_IMAGE' not found. Creating it from '$BASE_IMAGE'..."
    
    # Determine the project root directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
    
    echo "Building custom image with dependencies from Dockerfile..."
    # Use the project Dockerfile
    podman build \
        --build-arg BASE_IMAGE="$BASE_IMAGE" \
        -t "$BUILD_IMAGE" \
        -f "$PROJECT_DIR/Dockerfile" \
        --cgroup-manager=cgroupfs \
        "$PROJECT_DIR"
    
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create build image"
        exit 1
    fi
    
    echo "Build image '$BUILD_IMAGE' created successfully!"
fi

# Build using Podman container
echo "Building QScan using Podman container: $BUILD_IMAGE"

PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"

# Create build directory
mkdir -p "$BUILD_DIR"

# Run build in container
podman run --rm \
    -v "$PROJECT_DIR:/workspace:Z" \
    -w /workspace \
    "$BUILD_IMAGE" \
    bash -c "
        rm -rf build && \
        mkdir -p build && \
        cd build && \
        cmake .. -DQSCAN_ENABLE_GSTREAMER=ON && \
        make -j\$(nproc)
    "

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Binary is in: $BUILD_DIR/qscan"
    
    # Create AppImage if requested
    if [ "$CREATE_APPIMAGE" = "true" ]; then
        echo ""
        echo "========================================"
        echo "Creating AppImage with bundled libraries"
        echo "========================================"
        
        # Run AppImage creation in container
        podman run --rm \
            -v "$PROJECT_DIR:/workspace:Z" \
            -w /workspace \
            "$BUILD_IMAGE" \
            bash -c "bash /workspace/scripts/create-appimage.sh"
        
        if [ $? -eq 0 ]; then
            echo ""
            echo "========================================"
            echo "AppImage created successfully!"
            echo "========================================"
            ls -lh "$PROJECT_DIR"/*.AppImage 2>/dev/null || echo "AppImage file not found in expected location"
        else
            echo "Warning: AppImage creation failed"
        fi
    fi
else
    echo "Build failed"
    exit 1
fi

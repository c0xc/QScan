#!/bin/bash
# Build script for QScan using container with Qt 6 -> AppImage
#
# Usage: ./scripts/build.sh [--rebuild-container] [--no-gst]
#   --rebuild-container: Force rebuild of the container image
#   --no-gst: Build without GStreamer (forces QtCamera-only webcam backend if enabled)

set -e

# Build container env
OCI_ENGINE="${OCI_ENGINE:-podman}"
CREATE_APPIMAGE="${CREATE_APPIMAGE:-true}"
# Base container image (Qt environment)
#BASE_IMAGE="${BASE_IMAGE:-qt-6.4-fedora-36}"
BASE_IMAGE="${BASE_IMAGE:-qt-6.10.1-fedora}"
BUILD_IMAGE="qscan-fedora-36"

# CMake feature flags
CMAKE_FLAGS=(
    # -DQSCAN_ENABLE_GSTREAMER=ON
    -DQSCAN_ENABLE_QPDF=ON
    # -DQSCAN_ENABLE_OPENCV=ON
)

# Parse command line arguments
REBUILD_CONTAINER=false
for arg in "$@"; do
    case $arg in
        --rebuild-container)
            REBUILD_CONTAINER=true
            shift
            ;;
        --no-gst)
            for i in "${!CMAKE_FLAGS[@]}"; do
                if [[ "${CMAKE_FLAGS[$i]}" == -DQSCAN_ENABLE_GSTREAMER=* ]]; then
                    CMAKE_FLAGS[$i]='-DQSCAN_ENABLE_GSTREAMER=OFF'
                fi
            done
            shift
            ;;
    esac
done

# Check if container runtime is available
if ! command -v "$OCI_ENGINE" &> /dev/null; then
    echo "Error: $OCI_ENGINE is not installed"
    exit 1
fi

# Check if the base Qt container image exists
# if ! "$CONTAINER" image exists "$BASE_IMAGE"; then
#    echo "Warning: Base container image '$BASE_IMAGE' not found"
#    echo "Falling back to local build..."
#    
#    # Try local build with CMake
#    if command -v cmake &> /dev/null; then
#        echo "Building with CMake..."
#        mkdir -p build
#        cd build
#        cmake ..
#        make -j$(nproc)
#        cd ..
#        echo "Build complete. Binary is in build/qscan"
#        
#        # Create AppImage if requested
#        if [ "$CREATE_APPIMAGE" = "true" ]; then
#            echo "Creating AppImage..."
#            bash "$(dirname "$0")/create-appimage.sh"
#        fi
#    elif command -v qmake &> /dev/null; then
#        echo "Building with qmake..."
#        mkdir -p build
#        cd build
#        qmake ..
#        make -j$(nproc)
#        cd ..
#        echo "Build complete. Binary is in build/qscan"
#        
#        # Create AppImage if requested
#        if [ "$CREATE_APPIMAGE" = "true" ]; then
#            echo "Creating AppImage..."
#            bash "$(dirname "$0")/create-appimage.sh"
#        fi
#    else
#        echo "Error: Neither cmake nor qmake found"
#        exit 1
#    fi
#    exit 0
# fi

# Check if we need to rebuild the container image
if [ "$REBUILD_CONTAINER" = true ]; then
    echo "Forcing rebuild of container image '$BUILD_IMAGE'..."
    "$OCI_ENGINE" rmi -f "$BUILD_IMAGE" 2>/dev/null || true
fi

# Check if build image exists, create it if not
if ! "$OCI_ENGINE" image exists "$BUILD_IMAGE"; then
    echo "Build image '$BUILD_IMAGE' not found. Creating it from '$BASE_IMAGE'..."

    # Determine the project root directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

    # Create the container image
    echo "Creating container image: $BUILD_IMAGE from base image '$BASE_IMAGE'..."
    "$OCI_ENGINE" build \
        --build-arg BASE_IMAGE="$BASE_IMAGE" \
        -t "$BUILD_IMAGE" \
        -f "$PROJECT_DIR/scripts/Dockerfile" \
        --cgroup-manager=cgroupfs \
        "$PROJECT_DIR"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create build image"
        exit 1
    fi

    echo "Build image '$BUILD_IMAGE' created successfully!"
fi

echo "Building QScan in container: $BUILD_IMAGE"

PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build-appimage"

# Create build directory
mkdir -p "$BUILD_DIR"

# Run build in container
"$OCI_ENGINE" run --rm \
    -v "$PROJECT_DIR:/workspace:Z" \
    -w /workspace \
    "$BUILD_IMAGE" \
    bash -c "
        rm -rf build-appimage && \
        mkdir -p build-appimage && \
        cd build-appimage && \
        cmake .. ${CMAKE_FLAGS[*]} && \
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
        "$OCI_ENGINE" run --rm \
            -v "$PROJECT_DIR:/workspace:Z" \
            -w /workspace \
            "$BUILD_IMAGE" \
            bash -c "BUILD_DIR=/workspace/build-appimage bash /workspace/scripts/create-appimage.sh"
        
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

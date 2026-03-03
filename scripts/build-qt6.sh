#!/bin/bash
# Build script for QScan using container with Qt6 webcam backend (QtCamera)
# Variant: GStreamer OFF, QPDF OFF, OpenCV OFF (minimal)
# Output directory: build-qt6/
#
# Usage: ./scripts/build-qt6.sh [--rebuild-container]

set -e

OCI_ENGINE="${OCI_ENGINE:-podman}"
BASE_IMAGE="${BASE_IMAGE:-qt-6.10.1-fedora}"
BUILD_IMAGE="qscan-fedora-36"

CMAKE_FLAGS=(
    -DQSCAN_ENABLE_GSTREAMER=OFF
    -DQSCAN_ENABLE_QPDF=OFF
    -DQSCAN_ENABLE_OPENCV=OFF
)

REBUILD_CONTAINER=false

for arg in "$@"; do
    case $arg in
        --rebuild-container)
            REBUILD_CONTAINER=true
            shift
            ;;
    esac
done

if ! command -v "$OCI_ENGINE" &> /dev/null; then
    echo "Error: $OCI_ENGINE is not installed"
    exit 1
fi

if [ "$REBUILD_CONTAINER" = true ]; then
    echo "Forcing rebuild of container image '$BUILD_IMAGE'..."
    "$OCI_ENGINE" rmi -f "$BUILD_IMAGE" 2>/dev/null || true
fi

if ! "$OCI_ENGINE" image exists "$BUILD_IMAGE"; then
    echo "Build image '$BUILD_IMAGE' not found. Creating it from '$BASE_IMAGE'..."

    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

    "$OCI_ENGINE" build \
        --build-arg BASE_IMAGE="$BASE_IMAGE" \
        -t "$BUILD_IMAGE" \
        -f "$SCRIPT_DIR/Dockerfile" \
        --cgroup-manager=cgroupfs \
        "$PROJECT_DIR"
fi

echo "Building QScan in container: $BUILD_IMAGE"
echo "  - QSCAN_ENABLE_GSTREAMER=OFF"
echo "  - QSCAN_ENABLE_QPDF=OFF"
echo "  - QSCAN_ENABLE_OPENCV=OFF"

PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build-qt6"

mkdir -p "$BUILD_DIR"

"$OCI_ENGINE" run --rm \
    -v "$PROJECT_DIR:/workspace:Z" \
    -w /workspace \
    "$BUILD_IMAGE" \
    bash -c "
        rm -rf build-qt6 && \
        mkdir -p build-qt6 && \
        cd build-qt6 && \
        cmake .. ${CMAKE_FLAGS[*]} && \
        make -j\$(nproc)
    "

echo "Build successful!"
echo "Binary is in: $BUILD_DIR/qscan"

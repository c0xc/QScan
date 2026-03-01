#!/bin/bash
# Build script for QScan using Podman container with Qt 6
# Variant: GStreamer backend ON, QPDF OFF
# Output directory: build-gst/
#
# Usage: ./scripts/build-gst.sh [--rebuild-container]

set -e

OCI_ENGINE="${OCI_ENGINE:-podman}"

CMAKE_FLAGS=(
    -DQSCAN_ENABLE_GSTREAMER=ON
    -DQSCAN_ENABLE_QPDF=OFF
)

BASE_IMAGE="${BASE_IMAGE:-qt-6.10.1-fedora}"
BUILD_IMAGE="qscan-fedora-36"

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

echo "  - QSCAN_ENABLE_GSTREAMER=ON"
echo "  - QSCAN_ENABLE_QPDF=OFF"

PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build-gst"

mkdir -p "$BUILD_DIR"

"$OCI_ENGINE" run --rm \
    -v "$PROJECT_DIR:/workspace:Z" \
    -w /workspace \
    "$BUILD_IMAGE" \
    bash -c "
        rm -rf build-gst && \
        mkdir -p build-gst && \
        cd build-gst && \
        cmake .. ${CMAKE_FLAGS[*]} && \
        make -j\$(nproc)
    "

echo "Build successful!"
echo "Binary is in: $BUILD_DIR/qscan"

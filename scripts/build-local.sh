#!/bin/bash
cd "$(dirname "$0")/.."
rm -rf build
mkdir build
cd build

# Minimal build
# For Gst etc, we use the build container (build.sh)
# That's why those deps are disabled here:
#   The following required packages were not found: - gstreamer-app-1.0
cmake .. -DQSCAN_ENABLE_GSTREAMER=OFF -DQSCAN_ENABLE_QPDF=OFF
make -j$(nproc)

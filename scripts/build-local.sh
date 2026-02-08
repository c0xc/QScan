#!/bin/bash
cd "$(dirname "$0")/.."
rm -rf build
mkdir build
cd build
cmake .. 
#cmake .. -DQSCAN_ENABLE_GSTREAMER=ON
make -j$(nproc)

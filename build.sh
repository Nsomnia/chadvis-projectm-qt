#!/usr/bin/env bash
# Build script - Ninja with 1 core (potato-safe mode)
mkdir -p build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja -j1
echo ""
echo "=== Build complete ==="
echo "Binary: ./build/chadvis-projectm-qt"
echo "I use Arch, BTW."

#!/usr/bin/env bash
# Release build script - optimized with all cores

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

echo "=== Release Build ==="
echo "Working directory: $(pwd)"

# Clean first
if [ -d build ]; then
    echo "Cleaning previous build..."
    rm -rf build/*
fi

mkdir -p build
cd build

echo "Running CMake with Release flags..."
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Running Ninja (parallel build)..."
ninja

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo ""
echo "=== Release build complete ==="
echo "Binary: ./build/chadvis-projectm-qt"
echo "Size: $(ls -lh chadvis-projectm-qt | awk '{print $5}')"

#!/usr/bin/env bash
# Build script - Ninja with 1 core (potato-safe mode)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Starting Build ==="
echo "Working directory: $(pwd)"

# Check if CMakeCache.txt exists and has wrong paths
if [ -f build/CMakeCache.txt ]; then
    if grep -q "projectm-qt-visualizer" build/CMakeCache.txt 2>/dev/null; then
        echo "Found stale cache from old directory structure. Cleaning..."
        rm -rf build/*
    fi
fi

mkdir -p build
cd build

echo "Running CMake..."
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Running Ninja on all cores..."
# NOTE TODO: single core for potato-safe mode as a command line option
ninja -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo ""
echo "=== Build complete ==="
echo "Binary: ./build/chadvis-projectm-qt"
echo "I use Arch, BTW."

#!/usr/bin/env bash
# Build script - Debug mode
# "I use Arch, BTW" - and I compile my own visualizers
# NOTE: Uses ninja with 1 job for tactical grade potato computers
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Building projectm-qt-visualizer (Debug) ==="
echo "Project root: $PROJECT_ROOT"
echo "Using ninja with 1 job (potato-safe mode)"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -G Ninja \
-DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
"$PROJECT_ROOT"
cmake --build . -- -j1
echo ""
echo "=== Build complete ==="
echo "Binary: $BUILD_DIR/projectm-qt-visualizer"
echo "I use Arch, BTW. Build succeeded."

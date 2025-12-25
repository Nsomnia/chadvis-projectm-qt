#!/usr/bin/env bash
# Build script - Debug mode
# "I use Arch, BTW" - and I compile my own visualizers
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Building projectm-qt-visualizer (Debug) ==="
echo "Project root: $PROJECT_ROOT"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
"$PROJECT_ROOT"
cmake --build . --parallel $(nproc)
echo ""
echo "=== Build complete ==="
echo "Binary: $BUILD_DIR/src/projectm-qt-visualizer"
echo "I use Arch, BTW. Build succeeded."

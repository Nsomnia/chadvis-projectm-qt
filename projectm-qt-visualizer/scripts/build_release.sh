#!/usr/bin/env bash
# Release build with optimizations
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-release"
echo "=== Building projectm-qt-visualizer (Release) ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release \
"$PROJECT_ROOT"
cmake --build . --parallel $(nproc)
echo ""
echo "=== Release build complete ==="
echo "Binary: $BUILD_DIR/src/projectm-qt-visualizer"

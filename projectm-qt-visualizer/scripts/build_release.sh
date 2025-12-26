#!/usr/bin/env bash
# Release build with optimizations
# NOTE: Uses ninja with 1 job for tactical grade potato computers
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-release"
echo "=== Building projectm-qt-visualizer (Release) ==="
echo "Using ninja with 1 job (potato-safe mode)"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -G Ninja \
-DCMAKE_BUILD_TYPE=Release \
"$PROJECT_ROOT"
cmake --build . -- -j1
echo ""
echo "=== Release build complete ==="
echo "Binary: $BUILD_DIR/projectm-qt-visualizer"

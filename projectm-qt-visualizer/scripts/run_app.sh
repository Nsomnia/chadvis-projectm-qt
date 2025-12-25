#!/usr/bin/env bash
# Run the application
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/src/projectm-qt-visualizer"
if [ ! -f "$BINARY" ]; then
echo "Binary not found. Building first..."
"$SCRIPT_DIR/build.sh"
fi
echo "=== Running projectm-qt-visualizer ==="
echo "I use Arch, BTW."
exec "$BINARY" "$@"

#!/usr/bin/env bash
# Clean build - removes build dir and rebuilds
# Does NOT use rm -rf on anything important
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Clean Build ==="
# Only clean the build directory - this is safe
if [ -d "$BUILD_DIR" ]; then
echo "Removing $BUILD_DIR"
rm -rf "$BUILD_DIR" # OK because build/ is gitignored and regeneratable
fi
"$SCRIPT_DIR/build.sh"

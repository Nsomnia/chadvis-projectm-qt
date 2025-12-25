#!/usr/bin/env bash
# Run all tests
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Running Tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure
echo ""
echo "=== Tests complete ==="

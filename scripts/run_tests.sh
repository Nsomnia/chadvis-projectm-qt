#!/usr/bin/env bash
# Run all tests

cd /home/nsomnia/Documents/code/mimo-v2-flash-with-claude-inital-message

# Check if build exists
if [ ! -d ./build ]; then
    echo "Build directory not found. Building first..."
    ./scripts/build.sh
fi

cd build

echo "Running unit tests..."
if [ -f ./tests/unit/unit_tests ]; then
    ./tests/unit/unit_tests -v
else
    echo "Unit tests not found. They may need to be built separately."
fi

echo ""
echo "Running integration tests..."
if [ -f ./tests/integration/integration_tests ]; then
    ./tests/integration/integration_tests -v
else
    echo "Integration tests not found. They may need to be built separately."
fi

echo ""
echo "=== Test run complete ==="

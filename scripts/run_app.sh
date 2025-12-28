#!/usr/bin/env bash
# Run the application

cd /home/nsomnia/Documents/code/mimo-v2-flash-with-claude-inital-message

# Check if binary exists
if [ ! -f ./build/chadvis-projectm-qt ]; then
    echo "Binary not found. Building first..."
    ./scripts/build.sh
fi

echo "Running chadvis-projectm-qt..."
./build/chadvis-projectm-qt

#!/usr/bin/env bash
# Clean rebuild script - removes all build artifacts and rebuilds

echo "Cleaning build directory..."
cd /home/nsomnia/Documents/code/mimo-v2-flash-with-claude-inital-message
rm -rf build/*
rm -rf build-release 2>/dev/null

echo "Starting clean build..."
./scripts/build.sh

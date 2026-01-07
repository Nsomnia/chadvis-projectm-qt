#!/bin/bash
# run_debug.sh - Run ChadVis with logging

# Ensure logs directory exists
mkdir -p logs

# Timestamp for log file
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="logs/chadvis-debug_$TIMESTAMP.log"

echo "Starting ChadVis debug session..."
echo "Output being piped to: $LOG_FILE"

# Run the executable with debug flag and pipe both stdout and stderr
./build/chadvis-projectm-qt --debug "$@" 2>&1 | tee "$LOG_FILE"

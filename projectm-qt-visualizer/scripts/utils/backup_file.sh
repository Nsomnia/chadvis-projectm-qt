#!/usr/bin/env bash
# Safe "delete" - moves file to backup graveyard
# NEVER USE rm ON SOURCE FILES - USE THIS INSTEAD
set -e
if [ -z "$1" ]; then
echo "Usage: $0 <file_or_directory>"
echo "Moves the target to .backup_graveyard/ with timestamp"
exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
GRAVEYARD="$PROJECT_ROOT/.backup_graveyard"
TARGET="$1"
if [ ! -e "$TARGET" ]; then
echo "Error: $TARGET does not exist"
exit 1
fi
# Create graveyard if needed
mkdir -p "$GRAVEYARD"
# Generate timestamped name
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BASENAME=$(basename "$TARGET")
DEST="$GRAVEYARD/${TIMESTAMP}_${BASENAME}"
# Move to graveyard
mv "$TARGET" "$DEST"
# Log the action
echo "$(date -Iseconds) | $TARGET -> $DEST" >> "$GRAVEYARD/deletion_log.md"
echo "Backed up: $TARGET"
echo "Location: $DEST"
echo "Logged to: $GRAVEYARD/deletion_log.md"

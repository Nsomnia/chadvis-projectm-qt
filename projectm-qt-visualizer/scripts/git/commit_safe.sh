#!/usr/bin/env bash
# Safe commit with build verification
set -e
if [ -z "$1" ]; then
echo "Usage: $0 \"commit message\""
exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
echo "=== Safe Commit ==="
echo "Building to verify code compiles..."
cd "$PROJECT_ROOT"
./scripts/build.sh
echo ""
echo "Build successful. Committing..."
git add -A
git commit -m "$1"
echo ""
echo "=== Committed ==="
git log --oneline -1

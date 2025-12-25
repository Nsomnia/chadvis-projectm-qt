#!/usr/bin/env bash
# Create a pull request using gh CLI
set -e
BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" == "main" ] || [ "$BRANCH" == "master" ]; then
echo "Error: Cannot create PR from main/master branch"
exit 1
fi
echo "Creating PR for branch: $BRANCH"
gh pr create --fill

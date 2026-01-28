#!/usr/bin/env bash
# ChadVis Build Wrapper - Because some people still use bash in 2026.
# This script delegates the real work to the superior zsh script.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZSH_SCRIPT="${SCRIPT_DIR}/scripts/build.zsh"

if ! command -v zsh >/dev/null 2>&1; then
    echo "Error: zsh is required to run the build script."
    echo "Arch btw? Just run: sudo pacman -S zsh"
    exit 1
fi

# Ensure the script is executable, just in case a junior dev touched it
chmod +x "$ZSH_SCRIPT"

# Execute the real build script with all arguments
exec zsh "$ZSH_SCRIPT" "$@"

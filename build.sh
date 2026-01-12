#!/bin/bash
if [ -x "$(command -v zsh)" ]; then
    zsh "$(dirname "$0")/build.zsh" "$@"
else
    echo "Error: zsh is required to run the build script."
    echo "Please install zsh (e.g., sudo pacman -S zsh)"
    exit 1
fi

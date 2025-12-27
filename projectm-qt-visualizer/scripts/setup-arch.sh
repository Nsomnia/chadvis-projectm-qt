#!/bin/bash
# setup-arch.sh - One script to rule them all
# Run this and go make coffee. Or compile Gentoo, whatever takes longer.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_ok() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check if running on Arch
if [[ ! -f /etc/arch-release ]]; then
    log_err "This script is for Arch Linux. If you're on Ubuntu, you've made a life choice."
    log_err "Consider: https://wiki.archlinux.org/title/Installation_guide"
    exit 1
fi

log_info "Welcome to ChadVis setup. I use Arch btw."

# Install official repo packages
log_info "Installing packages from official repos..."
sudo pacman -S --needed --noconfirm \
    qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-svg \
    ffmpeg \
    taglib \
    tomlplusplus \
    spdlog fmt \
    glew glm \
    sdl2 \
    cmake ninja pkg-config \
    ttf-liberation ttf-dejavu \
    git base-devel

log_ok "Official packages installed"

# Check for AUR helper
AUR_HELPER=""
if command -v yay &> /dev/null; then
    AUR_HELPER="yay"
elif command -v paru &> /dev/null; then
    AUR_HELPER="paru"
else
    log_warn "No AUR helper found. Installing yay..."
    git clone https://aur.archlinux.org/yay-bin.git /tmp/yay-bin
    cd /tmp/yay-bin && makepkg -si --noconfirm
    AUR_HELPER="yay"
    cd - > /dev/null
fi

log_ok "AUR helper: $AUR_HELPER"

# Try to install ProjectM from AUR
log_info "Attempting to install ProjectM from AUR..."
if $AUR_HELPER -S --needed --noconfirm projectm 2>/dev/null; then
    log_ok "ProjectM installed from AUR"
else
    log_warn "AUR ProjectM failed or outdated. Building from source..."
    ./scripts/build-projectm.sh
fi

# Create config directory
mkdir -p ~/.config/chadvis-projectm-qt
if [[ ! -f ~/.config/chadvis-projectm-qt/config.toml ]]; then
    cp config/default.toml ~/.config/chadvis-projectm-qt/config.toml
    log_ok "Default config copied to ~/.config/chadvis-projectm-qt/"
fi

# Build the project
log_info "Building ChadVis..."
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

log_ok "Build complete!"
echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  ChadVis is ready. Run: ./build/chadvis-projectm-qt                 ║${NC}"
echo -e "${GREEN}║  Or install system-wide: sudo ninja -C build install      ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Pro tip: If it segfaults, that's a feature. File an issue anyway."
#!/usr/bin/env bash
# Check that all dependencies are installed
# For the true Arch user
echo "=== Checking Dependencies ==="
echo "I use Arch, BTW. Let's verify the setup."
echo ""
MISSING=()
# Check pacman packages
check_pkg() {
if pacman -Qi "$1" &>/dev/null; then
echo "[OK] $1"
else
echo "[MISSING] $1"
MISSING+=("$1")
fi
}
echo "--- Core Dependencies ---"
check_pkg "qt6-base"
check_pkg "qt6-multimedia"
check_pkg "libprojectm"
check_pkg "cmake"
check_pkg "gcc"
echo ""
echo "--- Development Tools ---"
check_pkg "git"
check_pkg "github-cli"
echo ""
echo "--- Optional (Terminal Browsers) ---"
check_pkg "lynx" || echo " (lynx is optional)"
check_pkg "links" || echo " (links is optional)"
echo ""
if [ ${#MISSING[@]} -eq 0 ]; then
echo "=== All dependencies satisfied! ==="
echo "Chad developer environment ready."
else
echo "=== Missing packages detected ==="
echo "Install with: sudo pacman -S ${MISSING[*]}"
exit 1
fi
# Verify projectM version
echo ""
echo "--- projectM Version Check ---"
PROJECTM_VER=$(pacman -Qi libprojectm 2>/dev/null | grep "Version" | awk '{print $3}')
if [[ "$PROJECTM_VER" == 4.* ]]; then
echo "[OK] libprojectm version $PROJECTM_VER (v4 API)"
else
echo "[WARNING] libprojectm version $PROJECTM_VER - expected 4.x"
fi

#!/usr/bin/env bash
# Check if all required dependencies are installed

echo "Checking dependencies for chadvis-projectm-qt..."

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check if a library exists
lib_exists() {
    ldconfig -p | grep -q "$1"
}

# Function to check if a Qt module exists
qt_module_exists() {
    pkg-config --exists "$1" 2>/dev/null
}

# Check essential build tools
echo ""
echo "=== Build Tools ==="
for tool in cmake ninja g++; do
    if command_exists $tool; then
        echo "✓ $tool: $(which $tool) ($($tool --version 2>/dev/null | head -1))"
    else
        echo "✗ $tool: NOT FOUND"
    fi
done

# Check Qt6 modules
echo ""
echo "=== Qt6 Modules ==="
for module in Qt6Core Qt6Gui Qt6Widgets Qt6Multimedia Qt6OpenGLWidgets Qt6Svg; do
    if qt_module_exists $module; then
        echo "✓ $module: $(pkg-config --modversion $module 2>/dev/null)"
    else
        echo "✗ $module: NOT FOUND"
    fi
done

# Check libraries
echo ""
echo "=== Libraries ==="
for lib in spdlog fmt taglib tomlplusplus glew glm; do
    if pkg-config --exists $lib 2>/dev/null; then
        echo "✓ $lib: $(pkg-config --modversion $lib 2>/dev/null)"
    else
        echo "✗ $lib: NOT FOUND"
    fi
done

# Check FFmpeg
echo ""
echo "=== FFmpeg ==="
for component in libavcodec libavformat libavutil libswscale libswresample; do
    if pkg-config --exists $component 2>/dev/null; then
        echo "✓ $component: $(pkg-config --modversion $component 2>/dev/null)"
    else
        echo "✗ $component: NOT FOUND"
    fi
done

# Check ProjectM
echo ""
echo "=== ProjectM ==="
if pkg-config --exists projectM-4 2>/dev/null; then
    echo "✓ projectM-4: $(pkg-config --modversion projectM-4 2>/dev/null)"
elif pkg-config --exists libprojectM 2>/dev/null; then
    echo "✓ libprojectM: $(pkg-config --modversion libprojectM 2>/dev/null)"
else
    echo "✗ projectM: NOT FOUND (Install from AUR: libprojectM)"
fi

# Check OpenGL
echo ""
echo "=== OpenGL ==="
if lib_exists "libGL.so"; then
    echo "✓ OpenGL: Found"
else
    echo "✗ OpenGL: NOT FOUND"
fi

echo ""
echo "=== Summary ==="
echo "If any dependencies are missing, install them with:"
echo "sudo pacman -S cmake ninja qt6-base qt6-multimedia qt6-svg spdlog fmt taglib tomlplusplus glew glm ffmpeg libprojectM"

 	#!/bin/bash
# install_projectm_v4_local.sh - Build ProjectM v4 locally for ChadVis

set -e

echo "=== Installing ProjectM v4 locally for ChadVis ==="
echo "System has v3, we need v4. Building locally..."

PROJECT_ROOT="$(pwd)"
EXTERNAL_DIR="$PROJECT_ROOT/external"
PROJECTM_DIR="$EXTERNAL_DIR/projectm"
INSTALL_DIR="$EXTERNAL_DIR/projectm-install"

# Create directories
mkdir -p "$EXTERNAL_DIR"

# Clone if not exists
if [ ! -d "$PROJECTM_DIR" ]; then
    echo "Cloning ProjectM v4..."
    git clone --depth 1 --branch v4.1.1 --recursive \
        https://github.com/projectM-visualizer/projectm.git "$PROJECTM_DIR"
else
    echo "ProjectM source already exists at $PROJECTM_DIR"
fi

cd "$PROJECTM_DIR"

# Clean previous build
# rm -rf build

# Configure for local install
echo "Configuring ProjectM v4..."
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DENABLE_PLAYLIST=ON \
    -DENABLE_SDL_UI=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=ON

# Build
echo "Building ProjectM v4..."
ninja -C build -j$(nproc)

# Install locally
echo "Installing to $INSTALL_DIR..."
ninja -C build install

# Verify
echo ""
echo "=== Verifying local installation ==="
ls -la "$INSTALL_DIR/include/projectM-4/" 2>/dev/null | head -5
ls -la "$INSTALL_DIR/lib/"libprojectM* 2>/dev/null

# Download presets if needed
PRESET_DIR="$INSTALL_DIR/share/projectM/presets"
if [ ! -d "$PRESET_DIR" ] || [ -z "$(ls -A $PRESET_DIR 2>/dev/null)" ]; then
    echo ""
    echo "Downloading presets..."
    mkdir -p "$PRESET_DIR"
    git clone --depth 1 \
        https://github.com/projectM-visualizer/presets-cream-of-the-crop.git \
        /tmp/projectm-presets-$$
    cp -r /tmp/projectm-presets-$$/* "$PRESET_DIR/"
    rm -rf /tmp/projectm-presets-$$
    echo "✓ Presets installed to $PRESET_DIR"
fi

echo ""
echo "=== ProjectM v4 local installation complete ==="
echo ""
echo "Presets location: $PRESET_DIR"

cd "$PROJECT_ROOT"

# Now update CMakeLists.txt to find local projectM
echo ""
echo "Updating CMakeLists.txt for local ProjectM..."

# Create a patch for CMakeLists.txt
cat > /tmp/projectm_cmake_patch.txt << 'PATCHEOF'

# Local ProjectM v4 installation
set(PROJECTM_LOCAL_DIR "${CMAKE_SOURCE_DIR}/external/projectm-install")
if(EXISTS "${PROJECTM_LOCAL_DIR}/include/projectM-4/projectM.h")
    message(STATUS "Using local ProjectM v4 from ${PROJECTM_LOCAL_DIR}")
    set(PROJECTM_INCLUDE_DIRS "${PROJECTM_LOCAL_DIR}/include")
    set(PROJECTM_LIBRARIES "${PROJECTM_LOCAL_DIR}/lib/libprojectM-4.so")
    set(PROJECTM_FOUND TRUE)
    
    # Add rpath so executable finds the library
    set(CMAKE_INSTALL_RPATH "${PROJECTM_LOCAL_DIR}/lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()
PATCHEOF

# Check if patch already applied
if grep -q "PROJECTM_LOCAL_DIR" CMakeLists.txt; then
    echo "CMakeLists.txt already patched for local ProjectM"
else
    # Find the line with pkg_check_modules(PROJECTM and insert before it
    sed -i '/pkg_check_modules(PROJECTM libprojectM)/i \
# Local ProjectM v4 installation\
set(PROJECTM_LOCAL_DIR "${CMAKE_SOURCE_DIR}/external/projectm-install")\
if(EXISTS "${PROJECTM_LOCAL_DIR}/include/projectM-4/projectM.h")\
    message(STATUS "Using local ProjectM v4 from ${PROJECTM_LOCAL_DIR}")\
    set(PROJECTM_INCLUDE_DIRS "${PROJECTM_LOCAL_DIR}/include")\
    set(PROJECTM_LIBRARIES "${PROJECTM_LOCAL_DIR}/lib/libprojectM-4.so")\
    set(PROJECTM_FOUND TRUE)\
    set(CMAKE_INSTALL_RPATH "${PROJECTM_LOCAL_DIR}/lib")\
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)\
endif()\
' CMakeLists.txt

    echo "✓ CMakeLists.txt patched"
fi

echo ""
echo "=== Now rebuild ChadVis ==="
echo "  rm -rf build"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "  make -C build -j\$(nproc)"




###


rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j$(nproc)


###


# Update default.toml preset path
sed -i 's|preset_path = .*|preset_path = "./external/projectm-install/share/projectM/presets"|' config/default.toml

#!/usr/bin/env bash
# ChadVis Build & Run Script
# Unified script for all development tasks

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# FIX: Remove problematic compiler flags that cause "Unknown argument" errors
# These flags can come from system config, Qt defaults, or environment variables
export CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-mno-direct-extern-access//g')
export CFLAGS=$(echo "$CFLAGS" | sed 's/-mno-direct-extern-access//g')
export CMAKE_CXX_FLAGS=$(echo "$CMAKE_CXX_FLAGS" | sed 's/-mno-direct-extern-access//g')
export CMAKE_C_FLAGS=$(echo "$CMAKE_C_FLAGS" | sed 's/-mno-direct-extern-access//g')

# Also filter from any Qt or system defaults
export QMAKE_CXXFLAGS=$(echo "$QMAKE_CXXFLAGS" | sed 's/-mno-direct-extern-access//g')

# FIX: Remove problematic compiler flags that cause build failures
# These flags can come from system config or environment
export CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-mno-direct-extern-access//g')
export CFLAGS=$(echo "$CFLAGS" | sed 's/-mno-direct-extern-access//g')

usage() {
    echo "Usage: $0 {build|release|clean|run|test|check-deps|help}"
    echo ""
    echo "Commands:"
    echo "  build      - Debug build (potato-safe, single core)"
    echo "  release    - Release build (optimized, parallel)"
    echo "  clean      - Clean build directory"
    echo "  run        - Build if needed and run the application"
    echo "  test       - Run unit and integration tests"
    echo "  check-deps - Check if all dependencies are installed"
    echo "  help       - Show this message"
    echo ""
    echo "Examples:"
    echo "  ./build.sh build      # Build debug version"
    echo "  ./build.sh run        # Build and run"
    echo "  ./build.sh test       # Run all tests"
}

build_debug() {
    echo "=== Starting Debug Build (Potato-Safe) ==="
    echo "Working directory: $(pwd)"
    
    # Check for stale cache
    if [ -f build/CMakeCache.txt ]; then
        if grep -q "projectm-qt-visualizer" build/CMakeCache.txt 2>/dev/null; then
            echo "Found stale cache from old directory structure. Cleaning..."
            rm -rf build/*
        fi
    fi
    
    mkdir -p build
    cd build
    
    echo "Running CMake..."
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
    
    if [ $? -ne 0 ]; then
        echo "CMake configuration failed!"
        exit 1
    fi
    
    echo "Running Ninja (single core for potato-safe)..."
    ninja -j$(nproc)
    
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
    
    echo ""
    echo "=== Debug build complete ==="
    echo "Binary: ./build/chadvis-projectm-qt"
    echo "Size: $(ls -lh chadvis-projectm-qt 2>/dev/null | awk '{print $5}' || echo 'unknown')"
}

build_release() {
    echo "=== Starting Release Build ==="
    echo "Working directory: $(pwd)"
    
    # Clean first
    if [ -d build ]; then
        echo "Cleaning previous build..."
        rm -rf build/*
    fi
    
    mkdir -p build
    cd build
    
    echo "Running CMake with Release flags..."
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
    
    if [ $? -ne 0 ]; then
        echo "CMake configuration failed!"
        exit 1
    fi
    
    echo "Running Ninja (parallel build)..."
    ninja
    
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
    
    echo ""
    echo "=== Release build complete ==="
    echo "Binary: ./build/chadvis-projectm-qt"
    echo "Size: $(ls -lh chadvis-projectm-qt | awk '{print $5}')"
}

clean_build() {
    echo "Cleaning build directory..."
    rm -rf build/*
    rm -rf build-release 2>/dev/null
    echo "Clean complete."
}

run_app() {
    echo "=== Running ChadVis ==="
    
    if [ ! -f ./build/chadvis-projectm-qt ]; then
        echo "Binary not found. Building first..."
        build_debug
    fi
    
    echo "Starting application..."
    ./build/chadvis-projectm-qt "$@"
}

run_tests() {
    echo "=== Running Tests ==="
    
    if [ ! -d ./build ]; then
        echo "Build directory not found. Building first..."
        build_debug
    fi
    
    cd build
    
    echo ""
    echo "--- Unit Tests ---"
    if [ -f ./tests/unit/unit_tests ]; then
        ./tests/unit/unit_tests -v
    else
        echo "Unit tests not found. Building..."
        cd ..
        build_debug
        cd build
        if [ -f ./tests/unit/unit_tests ]; then
            ./tests/unit/unit_tests -v
        else
            echo "Unit tests still not found."
        fi
    fi
    
    echo ""
    echo "--- Integration Tests ---"
    if [ -f ./tests/integration/integration_tests ]; then
        ./tests/integration/integration_tests -v
    else
        echo "Integration tests not found."
    fi
    
    echo ""
    echo "=== Test run complete ==="
}

check_deps() {
    echo "=== Checking Dependencies ==="
    
    missing=()
    
    # Check CMake
    if ! command -v cmake &> /dev/null; then
        missing+=("cmake")
    fi
    
    # Check Ninja
    if ! command -v ninja &> /dev/null; then
        missing+=("ninja")
    fi
    
    # Check Qt6
    if ! pkg-config --exists Qt6Core 2>/dev/null; then
        missing+=("qt6-base")
    fi
    
    # Check projectM
    if ! pkg-config --exists libprojectM-4 2>/dev/null; then
        missing+=("projectm")
    fi
    
    # Check FFmpeg
    if ! pkg-config --exists libavcodec 2>/dev/null; then
        missing+=("ffmpeg")
    fi
    
    if [ ${#missing[@]} -eq 0 ]; then
        echo "✓ All dependencies satisfied!"
        echo "You're ready to build, Chad."
    else
        echo "✗ Missing dependencies:"
        for dep in "${missing[@]}"; do
            echo "  - $dep"
        done
        echo ""
        echo "Install with: sudo pacman -S ${missing[*]}"
        exit 1
    fi
}

# Main command dispatcher
case "$1" in
    build)
        build_debug
        ;;
    release)
        build_release
        ;;
    clean)
        clean_build
        ;;
    run)
        shift
        run_app "$@"
        ;;
    test)
        run_tests
        ;;
    check-deps)
        check_deps
        ;;
    help|--help|-h)
        usage
        ;;
    "")
        usage
        exit 1
        ;;
    *)
        echo "Unknown command: $1"
        echo ""
        usage
        exit 1
        ;;
esac

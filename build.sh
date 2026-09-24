#!/usr/bin/env bash
# ChadVis portable build wrapper (macOS / Linux).
#
# Usage: ./build.sh [-d|--debug|-r|--release] [-c|--rebuild|--clean]
#
# Locates Qt6 via $CHADVIS_QT_PATH or common Homebrew/system prefixes,
# configures when needed, and builds through Ninja in ./build.
# The legacy Arch tooling was archived during housekeeping.

set -euo pipefail

BOLD=$'\033[1m'
CYAN=$'\033[1;36m'
GREEN=$'\033[32m'
YELLOW=$'\033[33m'
RED=$'\033[1;31m'
RESET=$'\033[0m'

usage() {
    printf '%sChadVis build%s\n' "$CYAN" "$RESET"
    printf '%sUsage:%s ./build.sh [options]\n\n' "$BOLD" "$RESET"
    printf '  %s(no flags)%s      incremental build; keeps current CMake settings\n' "$GREEN" "$RESET"
    printf '  %s-d, --debug%s      configure and build Debug\n' "$GREEN" "$RESET"
    printf '  %s-r, --release%s    configure and build Release\n' "$GREEN" "$RESET"
    printf '  %s--rebuild%s       archive build/ and perform a full rebuild\n' "$YELLOW" "$RESET"
    printf '  %s-c, --clean%s      alias for --rebuild\n' "$YELLOW" "$RESET"
    printf '  %s-h, --help%s      show this help\n' "$GREEN" "$RESET"
}

invalid_option() {
    printf '%sERROR%s build.sh: unknown option: %q\n' "$RED" "$RESET" "$1"
    printf '%sTry%s ./build.sh --help\n' "$YELLOW" "$RESET"
    exit 2
}

BUILD_TYPE=""
FULL_REBUILD=0
CLEAN_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -c|--rebuild|--clean)
            FULL_REBUILD=1
            shift
            ;;
        build)
            shift
            ;;
        release)
            BUILD_TYPE="Release"
            shift
            ;;
        rebuild)
            FULL_REBUILD=1
            shift
            ;;
        rebuild-release)
            BUILD_TYPE="Release"
            FULL_REBUILD=1
            shift
            ;;
        clean)
            CLEAN_ONLY=1
            shift
            ;;
        *)
            invalid_option "$1"
            ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build"

QT_PATH="${CHADVIS_QT_PATH:-}"
if [[ -z "$QT_PATH" ]]; then
    for candidate in \
        /opt/homebrew/opt/qt \
        /usr/local/opt/qt \
        /usr/lib/qt6; do
        if [[ -d "$candidate" ]]; then
            QT_PATH="$candidate"
            break
        fi
    done
fi

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
if [[ ! "$JOBS" =~ ^[0-9]+$ || "$JOBS" -lt 1 ]]; then
    JOBS=4
fi

cached_build_type() {
    local cache="${BUILD_DIR}/CMakeCache.txt"
    local value=""
    if [[ -f "$cache" ]]; then
        value="$(awk -F= '$1 ~ /^CMAKE_BUILD_TYPE(:STRING)?$/ { value = $2 } END { print value }' "$cache")"
    fi
    if [[ -n "$value" ]]; then
        printf '%s\n' "$value"
    else
        printf '%s\n' "Release"
    fi
}

archive_build_dir() {
    if [[ ! -d "$BUILD_DIR" ]]; then
        return
    fi

    local graveyard="${ROOT}/.backup_graveyard"
    local stamp
    local destination
    stamp="$(date +%Y%m%d-%H%M%S)"
    destination="${graveyard}/build-${stamp}"
    if [[ -e "$destination" ]]; then
        destination="${destination}-$$"
    fi
    mkdir -p "$graveyard"
    printf '%sARCHIVE%s %s -> %s\n' "$YELLOW" "$RESET" "$BUILD_DIR" "$destination"
    mv "$BUILD_DIR" "$destination"
}

configure_build() {
    local build_type="$1"
    local args=(-G Ninja -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$build_type")
    if [[ -n "$QT_PATH" ]]; then
        args+=(-DCMAKE_PREFIX_PATH="$QT_PATH")
    fi
    printf '%sCONFIGURE%s (%s, Qt: %s)\n' "$CYAN" "$RESET" "$build_type" "${QT_PATH:-autodetect}"
    cmake "${args[@]}"
}

if [[ $CLEAN_ONLY -eq 1 ]]; then
    archive_build_dir
    printf '%sOK%s build tree archived\n' "$GREEN" "$RESET"
    exit 0
fi

if [[ $FULL_REBUILD -eq 1 && -z "$BUILD_TYPE" ]]; then
    BUILD_TYPE="$(cached_build_type)"
fi

if [[ $FULL_REBUILD -eq 1 ]]; then
    archive_build_dir
fi

CONFIGURE=0
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    CONFIGURE=1
fi
if [[ -n "$BUILD_TYPE" ]]; then
    CONFIGURE=1
fi
if [[ $CONFIGURE -eq 1 ]]; then
    if [[ -z "$BUILD_TYPE" ]]; then
        BUILD_TYPE="$(cached_build_type)"
    fi
    configure_build "$BUILD_TYPE"
fi

printf '%sBUILD%s incremental (%s jobs)\n' "$CYAN" "$RESET" "$JOBS"
cmake --build "$BUILD_DIR" --parallel "$JOBS"
printf '%sDONE%s %s\n' "$GREEN" "$RESET" "${BUILD_DIR}/chadvis-projectm-qt"

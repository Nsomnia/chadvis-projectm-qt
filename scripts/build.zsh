#!/usr/bin/env zsh
# ═══ ChadVis Build System v1337 ═══
# "Arch btw" Edition - Optimized for Chads, safe for Juniors.
# 
# This script is so optimized it makes your CPU feel like it's on steroids.
# If you're a junior dev reading this: yes, we use zsh. No, we don't use bash.
# ══════════════════════════════════════════════════════════════════════════

setopt ERR_EXIT PIPE_FAIL NO_UNSET EXTENDED_GLOB

# ─── Configuration ──────────────────────────────────────────────────────────

readonly SCRIPT_DIR="${0:A:h}"
readonly PROJECT_ROOT="${SCRIPT_DIR:h}"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly LOG_DIR="${PROJECT_ROOT}/logs"
readonly LOG_FILE="${LOG_DIR}/build.log"
readonly BINARY_NAME="chadvis-projectm-qt"
readonly BINARY_PATH="${BUILD_DIR}/${BINARY_NAME}"

# Hardware Detection (Because we care about performance)
readonly CPU_CORES=$(nproc 2>/dev/null || echo 4)
readonly CPU_MODEL=$(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2 | sed 's/^[ \t]*//' || echo "Unknown Chad CPU")

# ─── Visuals (ANSI Escape Codes) ───────────────────────────────────────────

# Colors
readonly C_RESET=$'\e[0m'
readonly C_BOLD=$'\e[1m'
readonly C_RED=$'\e[31m'
readonly C_GREEN=$'\e[32m'
readonly C_YELLOW=$'\e[33m'
readonly C_BLUE=$'\e[34m'
readonly C_MAGENTA=$'\e[35m'
readonly C_CYAN=$'\e[36m'
readonly C_GRAY=$'\e[90m'

# Icons
readonly ICON_INFO="${C_BLUE}󰋽${C_RESET}"
readonly ICON_OK="${C_GREEN}󰄬${C_RESET}"
readonly ICON_WARN="${C_YELLOW}󱈸${C_RESET}"
readonly ICON_ERROR="${C_RED}󰅚${C_RESET}"
readonly ICON_CHAD="${C_CYAN}󰭹${C_RESET}"

# ─── Helper Functions ───────────────────────────────────────────────────────

log() {
    local type=$1
    local msg=$2
    case $type in
        info)  print -P "${ICON_INFO} ${msg}" ;;
        ok)    print -P "${ICON_OK} ${msg}" ;;
        warn)  print -P "${ICON_WARN} ${msg}" ;;
        error) print -P "${ICON_ERROR} ${C_BOLD}${msg}${C_RESET}" >&2 ;;
        header) print -P "\n${C_CYAN}═══ ${C_BOLD}${msg}${C_RESET}${C_CYAN} ═══${C_RESET}" ;;
        chad)  print -P "${ICON_CHAD} ${C_CYAN}${C_BOLD}${msg}${C_RESET}" ;;
    esac
}

log_kv() {
    print -P "${C_GRAY}  │${C_RESET} ${C_BOLD}${1}:${C_RESET} ${2}"
}

human_size() {
    [[ -f "$1" ]] || { echo "N/A"; return; }
    local size
    zstat -A size +size "$1" 2>/dev/null && numfmt --to=iec "$size" 2>/dev/null || echo "?"
}

show_help() {
    log chad "ChadVis Build System — Help"
    print -P "${C_BOLD}Usage:${C_RESET} build.zsh [options] [command]"
    print -P "\n${C_BOLD}Options:${C_RESET}"
    print -P "  -h, --help       Show this message (if you're lost)"
    print -P "  -d, --debug      Build in Debug mode (for when you break things)"
    print -P "  -r, --release    Build in Release mode (default, for Chads)"
    print -P "  -c, --clean      Clean build directory before building"
    print -P "  -i, --install    Install after successful build"
    print -P "  -j, --jobs <n>   Number of parallel jobs (default: ${CPU_CORES})"
    print -P "\n${C_BOLD}Commands:${C_RESET}"
    print -P "  build            Configure and compile (default)"
    print -P "  clean            Remove build artifacts"
    print -P "  run              Build and run the application"
    print -P "  test             Run the test suite"
    print -P "  check-deps       Verify system dependencies"
    print -P "\n${C_BOLD}Example:${C_RESET}"
    print -P "  ./build.sh -c -d build   # Clean, debug build"
}

# ─── Logic ──────────────────────────────────────────────────────────────────

mkdir -p "$LOG_DIR"

# Parse arguments
local -A opts
zparseopts -D -A opts h -help d -debug r -release c -clean i -install j: -jobs:

if [[ -n "${opts[(i)-h]}" || -n "${opts[(i)--help]}" ]]; then
    show_help
    exit 0
fi

BUILD_TYPE="Release"
[[ -n "${opts[(i)-d]}" || -n "${opts[(i)--debug]}" ]] && BUILD_TYPE="Debug"
[[ -n "${opts[(i)-r]}" || -n "${opts[(i)--release]}" ]] && BUILD_TYPE="Release"

JOBS=${opts[-j]:-${opts[--jobs]:-$CPU_CORES}}
CLEAN_FIRST=false
[[ -n "${opts[(i)-c]}" || -n "${opts[(i)--clean]}" ]] && CLEAN_FIRST=true

INSTALL_AFTER=false
[[ -n "${opts[(i)-i]}" || -n "${opts[(i)--install]}" ]] && INSTALL_AFTER=true

COMMAND=${1:-build}

# ─── Commands ───────────────────────────────────────────────────────────────

cmd_check_deps() {
    log header "Dependency Check"
    local -a deps=(
        "cmd:cmake:cmake" "cmd:ninja:ninja" "cmd:g++:gcc"
        "pkg:Qt6Core:qt6-base" "pkg:Qt6Widgets:qt6-base" "pkg:Qt6OpenGLWidgets:qt6-base"
        "pkg:libprojectM:libprojectm" "pkg:libavcodec:ffmpeg" "pkg:libavformat:ffmpeg"
    )
    local missing=0
    for spec in "${deps[@]}"; do
        local typ="${spec%%:*}"
        local rest="${spec#*:}"
        local chk="${rest%%:*}"
        local pkg="${rest#*:}"
        
        if [[ "$typ" == "cmd" ]]; then
            if ! command -v "$chk" &>/dev/null; then
                log error "Missing command: $chk (Install $pkg)"
                missing=$((missing + 1))
            else
                log ok "Found $chk"
            fi
        else
            if ! pkg-config --exists "$chk" 2>/dev/null; then
                log error "Missing package: $chk (Install $pkg)"
                missing=$((missing + 1))
            else
                log ok "Found $chk"
            fi
        fi
    done
    return $missing
}

cmd_clean() {
    log header "Cleaning"
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        log ok "Build directory nuked."
    else
        log info "Nothing to clean. You're already clean, Chad."
    fi
}

cmd_build() {
    log header "Building ChadVis"
    log_kv "CPU" "$CPU_MODEL"
    log_kv "Cores" "$CPU_CORES"
    log_kv "Type" "$BUILD_TYPE"
    log_kv "Jobs" "$JOBS"

    if [[ "$CLEAN_FIRST" == true ]]; then
        cmd_clean
    fi

    mkdir -p "$BUILD_DIR"

    log info "Configuring with CMake..."
    # Redirect to log file for LLMs to interpret
    if ! cmake -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -S "$PROJECT_ROOT" -B "$BUILD_DIR" > "$LOG_FILE" 2>&1; then
        log error "CMake configuration failed! Check $LOG_FILE"
        return 1
    fi

    log info "Compiling with Ninja..."
    if ! ninja -C "$BUILD_DIR" -j "$JOBS" >> "$LOG_FILE" 2>&1; then
        log error "Compilation failed! Check $LOG_FILE"
        # Keep the log file for debugging
        return 1
    fi

    log ok "Build successful!"
    log_kv "Binary" "$BINARY_PATH"
    log_kv "Size" "$(human_size "$BINARY_PATH")"

    # Clear log file on success so we don't confuse anyone
    truncate -s 0 "$LOG_FILE"
    
    if [[ "$INSTALL_AFTER" == true ]]; then
        cmd_install
    fi
}

cmd_install() {
    log header "Installing"
    log info "Running installation (might need sudo)..."
    if ! sudo ninja -C "$BUILD_DIR" install >> "$LOG_FILE" 2>&1; then
        log error "Installation failed! Check $LOG_FILE"
        return 1
    fi
    log ok "Installed successfully. Go forth and visualize."
}

cmd_run() {
    if [[ ! -x "$BINARY_PATH" ]]; then
        log warn "Binary not found. Building first..."
        cmd_build || return 1
    fi
    log header "Launching ChadVis"
    exec "$BINARY_PATH" "$@"
}

cmd_test() {
    log header "Running Tests"
    if [[ ! -d "$BUILD_DIR" ]]; then
        cmd_build || return 1
    fi
    # Assuming tests are built into build/tests/
    if [[ -d "$BUILD_DIR/tests" ]]; then
        ctest --test-dir "$BUILD_DIR" --output-on-failure
    else
        log warn "No tests found. Did you build them?"
    fi
}

# ─── Main ───────────────────────────────────────────────────────────────────

case $COMMAND in
    build)      cmd_build ;;
    clean)      cmd_clean ;;
    run)        cmd_run ;;
    test)       cmd_test ;;
    check-deps) cmd_check_deps ;;
    install)    cmd_install ;;
    *)          log error "Unknown command: $COMMAND"; show_help; exit 1 ;;
esac

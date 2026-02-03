#!/usr/bin/env zsh
# ═══ ChadVis Build System v1337.2 ═══
# "Arch btw" Edition - Now with dual output and auto-install!
# 
# Refactored to use Zsh built-ins because external commands are for Juniors.
# ══════════════════════════════════════════════════════════════════════════

setopt ERR_EXIT PIPE_FAIL NO_UNSET EXTENDED_GLOB

# Load Chad modules
zmodload zsh/datetime
zmodload zsh/stat 2>/dev/null || true
zmodload zsh/parameter 2>/dev/null || true

# ─── Configuration ──────────────────────────────────────────────────────────

readonly SCRIPT_DIR="${0:A:h}"
readonly PROJECT_ROOT="${SCRIPT_DIR:h}"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly LOG_DIR="${PROJECT_ROOT}/logs"
readonly LOG_FILE="${LOG_DIR}/build.log"
readonly BINARY_NAME="chadvis-projectm-qt"
readonly BINARY_PATH="${BUILD_DIR}/${BINARY_NAME}"

# Hardware Detection (Zsh-native, no grep/sed/nproc bloat)
local cpu_cores
if (( ${+commands[getconf]} )); then
    cpu_cores=$(getconf _NPROCESSORS_ONLN)
else
    local cpuinfo=(${(f)"$(< /proc/cpuinfo)"})
    cpu_cores=${#${(M)cpuinfo:#processor*}}
fi
readonly CPU_CORES=${cpu_cores:-4}

local cpu_model="Unknown Chad CPU"
if [[ -f /proc/cpuinfo ]]; then
    local model_line=${(M)${(f)"$(< /proc/cpuinfo)"}:#model name*}
    cpu_model=${model_line#*: }
fi
readonly CPU_MODEL=${cpu_model}

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
    if zstat -A size +size "$1" 2>/dev/null; then
        # Zsh-native size formatting
        if (( size > 1024 * 1024 * 1024 )); then
            printf "%.2f GiB" $(( size / 1024.0 / 1024.0 / 1024.0 ))
        elif (( size > 1024 * 1024 )); then
            printf "%.2f MiB" $(( size / 1024.0 / 1024.0 ))
        elif (( size > 1024 )); then
            printf "%.2f KiB" $(( size / 1024.0 ))
        else
            echo "${size} B"
        fi
    else
        echo "?"
    fi
}

# Run command with output to both console and log file
# Usage: run_logged <log_file> <command> [args...]
run_logged() {
    local log_file="$1"
    shift
    
    # Clear log file BEFORE running command
    : > "$log_file"
    
    # Run command with unbuffered output to both console and log
    # stdbuf -oL -eL makes stdout and stderr line-buffered for immediate display
    # Use && sync to ensure log is written if process exits quickly
    if (( ${+commands[stdbuf]} )); then
        stdbuf -oL -eL "$@" 2>&1 | tee -a "$log_file" && sync
    else
        "$@" 2>&1 | tee -a "$log_file" && sync
    fi
    
    # Return the exit status of the command (not tee)
    return $pipestatus[1]
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
    print -P "  -y, --yes        Auto-install missing dependencies without prompting"
    print -P "\n${C_BOLD}Commands:${C_RESET}"
    print -P "  build            Configure and compile (default)"
    print -P "  clean            Remove build artifacts"
    print -P "  run              Build and run the application"
    print -P "  test             Run the test suite"
    print -P "  check-deps       Verify system dependencies"
    print -P "\n${C_BOLD}Example:${C_RESET}"
    print -P "  ./build.sh -c -d build   # Clean, debug build"
    print -P "  ./build.sh -y build      # Auto-install deps if missing"
}

# ─── Logic ──────────────────────────────────────────────────────────────────

mkdir -p "$LOG_DIR"

# Parse arguments using zparseopts (Zsh-native)
local -A opts
zparseopts -D -A opts h -help d -debug r -release c -clean i -install j: -jobs: y -yes

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

AUTO_INSTALL=false
[[ -n "${opts[(i)-y]}" || -n "${opts[(i)--yes]}" ]] && AUTO_INSTALL=true

COMMAND=${1:-build}

# ─── Commands ───────────────────────────────────────────────────────────────

cmd_check_deps() {
    log header "Dependency Check"
    
    local is_arch=false
    [[ -f /etc/arch-release ]] && is_arch=true

    # Expanded dependency list - organized by category
    local -a deps=(
        # Build tools
        "cmd:cmake:cmake" "cmd:ninja:ninja" "cmd:g++:gcc"
        # Qt6 base packages
        "pkg:Qt6Core:qt6-base" "pkg:Qt6Widgets:qt6-base" "pkg:Qt6OpenGLWidgets:qt6-base"
        "pkg:Qt6Network:qt6-base" "pkg:Qt6Sql:qt6-base"
        # Qt6 multimedia (separate package)
        "pkg:Qt6Multimedia:qt6-multimedia"
        # Qt6 extras
        "pkg:Qt6Svg:qt6-svg"
        # Core libraries
        "pkg:projectM4:libprojectm" "pkg:libavcodec:ffmpeg" "pkg:libavformat:ffmpeg"
        "pkg:libavutil:ffmpeg" "pkg:libswresample:ffmpeg"
        # Other dependencies
        "pkg:spdlog:spdlog" "pkg:fmt:format" "pkg:taglib:taglib"
        "pkg:sqlite3:sqlite"
    )
    
    local -a missing_pkgs=()
    local missing_count=0

    for spec in "${deps[@]}"; do
        local typ="${spec%%:*}"
        local rest="${spec#*:}"
        local chk="${rest%%:*}"
        local pkg="${rest#*:}"
        
        local found=false
        if [[ "$typ" == "cmd" ]]; then
            (( ${+commands[$chk]} )) && found=true
        else
            # Try pkg-config first
            pkg-config --exists "$chk" 2>/dev/null && found=true
            # Fallback for Arch: check pacman directly
            if [[ "$found" == false && "$is_arch" == true ]]; then
                pacman -Qq "$pkg" &>/dev/null && found=true
            fi
        fi

        if [[ "$found" == true ]]; then
            log ok "Found $chk"
        else
            log error "Missing: $chk (Package: $pkg)"
            missing_pkgs+=("$pkg")
            missing_count=$((missing_count + 1))
        fi
    done

    if (( ${#missing_pkgs[@]} > 0 )); then
        log warn "Missing dependencies detected!"
        
        if [[ "$is_arch" == true ]]; then
            # Remove duplicates from missing packages
            local -a unique_missing=(${(u)missing_pkgs})
            
            print -P "\n${C_BOLD}Packages to install:${C_RESET} ${(j: :)unique_missing}"
            print -P "${C_GRAY}Install command: sudo pacman -S --needed ${(j: :)unique_missing}${C_RESET}"
            
            if [[ "$AUTO_INSTALL" == true ]]; then
                log info "Auto-install enabled. Installing missing packages..."
                if sudo pacman -S --needed "${unique_missing[@]}"; then
                    log ok "Dependencies installed successfully!"
                    # Re-check to confirm
                    cmd_check_deps
                    return $?
                else
                    log error "Failed to install dependencies"
                    return 1
                fi
            else
                print -P "\n${C_YELLOW}Run with -y flag to auto-install:${C_RESET} ./build.sh -y $COMMAND"
                print -P "${C_YELLOW}Or install manually:${C_RESET} sudo pacman -S --needed ${(j: :)unique_missing}"
            fi
        else
            log warn "Auto-install only supported on Arch Linux."
            log info "Please install the missing packages manually."
        fi
    fi

    return $missing_count
}

cmd_build() {
    local start_time=$EPOCHSECONDS
    
    # Auto-check deps before building, because we're Chads
    cmd_check_deps || { 
        log error "Dependencies missing. Fix them, Junior."
        log info "Hint: Run './build.sh -y build' to auto-install on Arch."
        return 1 
    }

    log header "Building ChadVis"
    log_kv "CPU" "$CPU_MODEL"
    log_kv "Cores" "$CPU_CORES"
    log_kv "Type" "$BUILD_TYPE"
    log_kv "Jobs" "$JOBS"
    log_kv "Log" "$LOG_FILE"

    if [[ "$CLEAN_FIRST" == true ]]; then
        cmd_clean
    fi

    mkdir -p "$BUILD_DIR"

    log info "Configuring with CMake..."
    log info "Output: Console + $LOG_FILE"
    
    # Run cmake with output to console and log file
    if ! run_logged "$LOG_FILE" cmake -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -S "$PROJECT_ROOT" -B "$BUILD_DIR"; then
        
        log error "CMake configuration failed!"
        log info "Check $LOG_FILE for full details."
        log info "Last 10 lines of error:"
        tail -n 10 "$LOG_FILE" | while read line; do
            print -P "${C_RED}  │${C_RESET} $line"
        done
        return 1
    fi

    log info "Compiling with Ninja ($JOBS jobs)..."
    log info "Output: Console + $LOG_FILE"
    
    # Run ninja with output to console and log file
    if ! run_logged "$LOG_FILE" ninja -C "$BUILD_DIR" -j "$JOBS"; then
        log error "Compilation failed!"
        log info "Check $LOG_FILE for full details."
        log info "Last 20 lines of error:"
        tail -n 20 "$LOG_FILE" | while read line; do
            print -P "${C_RED}  │${C_RESET} $line"
        done
        return 1
    fi

    local end_time=$EPOCHSECONDS
    local duration=$(( end_time - start_time ))

    log ok "Build successful! (Took ${duration}s)"
    log_kv "Binary" "$BINARY_PATH"
    log_kv "Size" "$(human_size "$BINARY_PATH")"

    # Reminder for the Chads
    print -P "\n${ICON_CHAD} ${C_CYAN}${C_BOLD}REMINDER:${C_RESET} Did you update ${C_BOLD}CHANGELOG.md${C_RESET} for these major gains?"

    # Do not clear log file on success, user wants to see output if needed
    # : > "$LOG_FILE"
    
    if [[ "$INSTALL_AFTER" == true ]]; then
        cmd_install
    fi
}

cmd_clean() {
    log header "Cleaning"
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        log ok "Build directory Krush Killed and Destryoed witta big ol' nuke!"
    else
        log info "Nothing to clean. You're already clean, Chad."
    fi
}

cmd_install() {
    log header "Installing"
    log info "Running installation (might need sudo)..."
    if ! run_logged "$LOG_FILE" sudo ninja -C "$BUILD_DIR" install; then
        log error "Installation failed!"
        log info "Check $LOG_FILE for full details."
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
        run_logged "$LOG_FILE" ctest --test-dir "$BUILD_DIR" --output-on-failure
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

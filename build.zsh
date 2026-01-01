#!/usr/bin/env zsh

setopt ERR_EXIT PIPE_FAIL NO_UNSET EXTENDED_GLOB
zmodload zsh/stat 2>/dev/null || true

readonly SCRIPT_DIR="${0:A:h}"
readonly BUILD_DIR="${SCRIPT_DIR}/build"
readonly BINARY_NAME="chadvis-projectm-qt"
readonly BINARY_PATH="${BUILD_DIR}/${BINARY_NAME}"
readonly N4500_CORES=2
readonly N4500_ARCH="tremont"

readonly -a CPU_OPT_FLAGS=(
    "-march=${N4500_ARCH}" "-mtune=${N4500_ARCH}"
    "-msse4.2" "-mpopcnt" "-maes" "-mno-avx" "-mno-avx2"
)
readonly -a COMMON_OPT_FLAGS=("-pipe" "-fomit-frame-pointer" "-ffunction-sections" "-fdata-sections")
readonly -a DEBUG_FLAGS=("-O2" "-g1")
readonly -a RELEASE_FLAGS=("-O3" "-flto=${N4500_CORES}" "-fno-plt" "-DNDEBUG")
readonly -a LINKER_FLAGS=("-Wl,--gc-sections" "-Wl,--as-needed")

join() { local IFS="$1"; shift; print -r -- "$*"; }

compose_flags() {
    local -a all_flags=()
    local arr_name
    for arr_name in "$@"; do all_flags+=("${(P@)arr_name}"); done
    join ' ' "${all_flags[@]}"
}

build_cflags() {
    case "$1" in
        Debug)   compose_flags CPU_OPT_FLAGS COMMON_OPT_FLAGS DEBUG_FLAGS ;;
        Release) compose_flags CPU_OPT_FLAGS COMMON_OPT_FLAGS RELEASE_FLAGS ;;
        *)       compose_flags CPU_OPT_FLAGS COMMON_OPT_FLAGS ;;
    esac
}

build_ldflags() {
    local base="$(join ' ' "${LINKER_FLAGS[@]}")"
    [[ "$1" == "Release" ]] && base+=" -flto=${N4500_CORES}"
    print -r -- "$base"
}

human_size() {
    [[ -f "$1" ]] || { print "N/A"; return; }
    local size
    zstat -A size +size "$1" 2>/dev/null && numfmt --to=iec "$size" 2>/dev/null || print "?"
}

log() {
    local -A s=([info]="%F{blue}▸%f" [ok]="%F{green}✓%f" [warn]="%F{yellow}⚠%f" [error]="%F{red}✗%f" [header]="%F{cyan}═══%f")
    case "$1" in
        header) print -P "\n${s[header]} %B${2}%b ${s[header]}" ;;
        error)  print -P "${s[$1]} ${2}" >&2 ;;
        *)      print -P "${s[$1]:-▸} ${2}" ;;
    esac
}

log_kv() { print -P "%F{8}  │%f %F{white}${1}:%f ${2}"; }

remove_problematic_flags() {
    local input="${1:-}"
    local -a bad=("-mno-direct-extern-access" "-fcf-protection" "-fstack-clash-protection")
    for f in "${bad[@]}"; do input="${input//${f}/}"; done
    print -r -- "${${input//  / }## }"
}

sanitize_environment() {
    local -a vars=(CXXFLAGS CFLAGS CMAKE_CXX_FLAGS CMAKE_C_FLAGS QMAKE_CXXFLAGS)
    for v in "${vars[@]}"; do
        [[ -n "${(P)v:-}" ]] && export "${v}=$(remove_problematic_flags "${(P)v}")"
    done
}

ensure_build_dir() {
    if [[ "$1" == true && -d "$BUILD_DIR" ]]; then
        log info "Cleaning previous build..."
        rm -rf "${BUILD_DIR:?}"/*
    fi
    mkdir -p "$BUILD_DIR"
}

invoke_cmake() {
    local build_type="$1"
    local cxxflags="$(build_cflags "$build_type")"
    local ldflags="$(build_ldflags "$build_type")"
    
    log info "Configuring CMake (${build_type})..."
    log_kv "CXX Flags" "$cxxflags"
    log_kv "Linker Flags" "$ldflags"
    
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_CXX_COMPILER="g++" \
        -DCMAKE_CXX_FLAGS="$cxxflags" \
        -DCMAKE_EXE_LINKER_FLAGS="$ldflags" \
        -DCMAKE_SHARED_LINKER_FLAGS="$ldflags" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -S "$SCRIPT_DIR" -B "$BUILD_DIR"
}

invoke_ninja() {
    log info "Building with Ninja (${1:-$N4500_CORES} jobs)..."
    NINJA_STATUS="[%f/%t %es] " ninja -C "$BUILD_DIR" -j "${1:-$N4500_CORES}"
}

build_pipeline() {
    local build_type="$1" clean_first="${2:-false}"
    
    log header "${build_type} Build"
    log_kv "Architecture" "$N4500_ARCH"
    log_kv "Parallelism" "${N4500_CORES} jobs"
    
    sanitize_environment
    ensure_build_dir "$clean_first" || return 1
    invoke_cmake "$build_type" || { log error "CMake failed"; return 1; }
    invoke_ninja "$N4500_CORES" || { log error "Build failed"; return 1; }
    
    log ok "Build complete"
    log_kv "Binary" "$BINARY_PATH"
    log_kv "Size" "$(human_size "$BINARY_PATH")"
}

ensure_binary() {
    [[ -x "$BINARY_PATH" ]] || { log warn "Binary not found, building..."; cmd_build || return 1; }
}

cmd_build()   { build_pipeline "Debug" false; }
cmd_release() { build_pipeline "Release" false; }
cmd_rebuild() { build_pipeline "Debug" true; }
cmd_rebuild_release() { build_pipeline "Release" true; }

cmd_clean() {
    log header "Clean"
    for t in "$BUILD_DIR" "${SCRIPT_DIR}/build-release"; do
        [[ -d "$t" ]] && { rm -rf "${t:?}"/*; log ok "Cleaned: ${t}"; }
    done
    log ok "Clean complete"
}

cmd_run() {
    log header "Run"
    ensure_binary || return 1
    exec "$BINARY_PATH" "$@"
}

cmd_test() {
    log header "Test Suite"
    ensure_binary || return 1
    for spec in "tests/unit/unit_tests:Unit" "tests/integration/integration_tests:Integration"; do
        local p="${BUILD_DIR}/${spec%%:*}" n="${spec##*:}"
        log info "Running ${n}..."
        [[ -x "$p" ]] && { "$p" -v && log ok "${n} passed" || log warn "${n} failed"; } || log warn "${n} not found"
    done
}

cmd_check_deps() {
    log header "Dependency Check"
    local -a deps=(
        "cmd:cmake:cmake" "cmd:ninja:ninja" "cmd:g++:gcc"
        "pkg:Qt6Core:qt6-base" "pkg:Qt6Widgets:qt6-base" "pkg:Qt6OpenGLWidgets:qt6-base"
        "pkg:libprojectM-4:projectm" "pkg:libavcodec:ffmpeg" "pkg:libavformat:ffmpeg" "pkg:libpulse:libpulse"
    )
    local -a missing=() found=()
    for spec in "${deps[@]}"; do
        local typ="${spec%%:*}" rest="${spec#*:}" chk="${rest%%:*}" pkg="${rest#*:}"
        case "$typ" in
            cmd) command -v "$chk" &>/dev/null ;;
            pkg) pkg-config --exists "$chk" 2>/dev/null ;;
        esac && found+=("$pkg") || missing+=("$pkg")
    done
    local -aU uf=("${found[@]}") um=("${missing[@]}")
    for p in "${uf[@]}"; do log ok "$p"; done
    (( ${#um[@]} == 0 )) && { log ok "All dependencies satisfied"; return 0; }
    log error "Missing: ${um[*]}"
    print "    sudo pacman -S ${um[*]}"
    return 1
}

cmd_help() {
    print -P "%F{cyan}ChadVis Build Script — Intel N4500 Optimized%f"
    print -P "%BUsage:%b ${0:t} <command> [args...]"
    print -P "%BCommands:%b"
    print "  build           Incremental debug build"
    print "  release         Incremental release build (LTO)"
    print "  rebuild         Clean debug build"
    print "  rebuild-release Clean release build"
    print "  clean           Remove build artifacts"
    print "  run [args]      Build if needed, launch"
    print "  test            Run tests"
    print "  check-deps      Verify packages"
    print "  help            This message"
}

typeset -A DISPATCH=(
    [build]=cmd_build [release]=cmd_release [rebuild]=cmd_rebuild [rebuild-release]=cmd_rebuild_release
    [clean]=cmd_clean [run]=cmd_run [test]=cmd_test
    [check-deps]=cmd_check_deps [check]=cmd_check_deps [deps]=cmd_check_deps
    [help]=cmd_help [-h]=cmd_help [--help]=cmd_help
)

main() {
    cd "$SCRIPT_DIR" || exit 1
    local cmd="${1:-help}"; [[ $# -gt 0 ]] && shift
    local h="${DISPATCH[$cmd]:-}"
    [[ -n "$h" ]] && "$h" "$@" || { log error "Unknown: ${cmd}"; cmd_help; return 1; }
}

main "$@"

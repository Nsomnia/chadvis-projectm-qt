#!/usr/bin/env bash
# ChadVis build wrapper (macOS / Linux) with per-profile build directories.
#
# The point of this script is iteration speed. Two things dominate that on a
# four-core box, and both are addressed here rather than left to the operator:
#
#   1. Switching CMAKE_BUILD_TYPE on an existing build directory forces a full
#      recompile, because every object is invalidated. So every profile gets its
#      OWN directory (build/, build-fast/, build-tsan/, ...). Changing profile
#      costs one configure the first time and nothing after that.
#
#   2. Building everything to run one test is most of the build. --tests builds
#      only the test binaries, which skips the app target, the QML qmltypes
#      generation and the integration GL suite. ccache then makes every rebuild
#      after an unrelated edit close to free.
#
# Usage: ./build.sh [--fast|--debug|--tsan|--asan|--ubsan|--release] [actions]
#        ./build.sh --help

set -euo pipefail

BOLD=$'\033[1m'
CYAN=$'\033[1;36m'
GREEN=$'\033[32m'
YELLOW=$'\033[33m'
RED=$'\033[1;31m'
RESET=$'\033[0m'

# Every ctest entry that is safe to run headless on a machine with no audio
# device and no drawable. integration_tests constructs a real AudioEngine and
# integration_gl_tests needs a real GL context, so neither gates a check here.
#
# This list is ALSO the --tests build target list -- it is passed straight to
# `cmake --build --target` -- which makes it a load-bearing allow-list rather than
# documentation, and it cuts BOTH ways:
#
#   * A ctest entry missing from it is never built by `--tests` and never run by
#     `--test`. test_RenderExecutor existed, was registered with ctest, and was
#     invisible for exactly that reason: the same silent-omission class as the
#     undeclared cmake source, one level up.
#   * An entry LEFT in it after its target is deleted fails the build outright
#     with `ninja: unknown target 'test_AudioAnalyzer'`. Which happened the day
#     AudioAnalyzer was retired.
#
# So this list has to be edited in the same commit as any add_executable/
# add_test or any removal of one. Verify with:
#   comm -3 <(ctest --test-dir build-fast/tests -N | sed -n 's/.*: //p' | grep -v '^Total') \
#           <(printf '%s\n' unit_tests "${SAFE_TESTS[@]}")
# which should print nothing.
SAFE_TESTS=(
    unit_tests
    test_SunoAudioUploadService
    test_SunoRequestEpoch
    test_SunoExploreService
    test_SunoNotificationService
    test_ClerkAuthClient
    test_HttpPolicy
    test_RenderExecutor
)

usage() {
    cat <<EOF
${BOLD}ChadVis build${RESET}

${BOLD}Profiles${RESET} ${YELLOW}(each keeps its own build directory; switch freely)${RESET}
  ${GREEN}(no flags)${RESET}       Release, ./build            the shipping configuration
  ${GREEN}-f, --fast${RESET}       -O0 -g0 + ccache, ./build-fast  the edit/test loop
  ${GREEN}-d, --debug${RESET}      Debug with debug info, ./build-debug
  ${GREEN}-r, --release${RESET}    same as no flags
  ${GREEN}--tsan${RESET}           ThreadSanitizer, ./build-tsan  ${YELLOW}(exits 66 on Qt-internal reports; see cmake/tsan.supp)${RESET}
  ${GREEN}--asan${RESET}           AddressSanitizer, ./build-asan
  ${GREEN}--ubsan${RESET}          Undefined+AddressSanitizer, ./build-ubsan

${BOLD}Actions${RESET}
  ${GREEN}--tests${RESET}          build only the test binaries (skips app + QML tooling)
  ${GREEN}--test [name]${RESET}    run ctest in <profile>/tests; ${GREEN}name${RESET} is a test-name regex
  ${GREEN}--safe${RESET}           run only the headless-safe entries (default for --test)
  ${GREEN}--run [qtest args]${RESET} run unit_tests directly, e.g. --run aHealthyRecording
  ${GREEN}--rebuild${RESET}        archive this profile's build directory, then full rebuild
  ${GREEN}-c, --clean${RESET}      archive this profile's build directory and stop
  ${GREEN}-h, --help${RESET}       this text

${BOLD}Tuning${RESET}
  ${GREEN}-j, --jobs N${RESET}     parallel jobs (default: cores, minus one if not rootless-sandboxed)
  ${GREEN}--no-ccache${RESET}      do not use ccache even if installed
  ${GREEN}--qt PATH${RESET}        Qt6 prefix (else \$CHADVIS_QT_PATH, then Homebrew prefixes)

${BOLD}Examples${RESET}
  ./build.sh --fast --tests --test       # tight loop: build tests, run the headless-safe suites
  ./build.sh --fast --run aHealthy       # tightest loop: one QTest function, no ctest
  ./build.sh                              # Release, everything, for a real check
  ./build.sh --test unit_tests            # Release tests only, ctest all entries
EOF
}

die() {
    printf '%sERROR%s %s\n' "$RED" "$RESET" "$1" >&2
    exit 1
}

warn() {
    printf '%sWARN%s %s\n' "$YELLOW" "$RESET" "$1" >&2
}

# --------------------------------------------------------------------------
# Option parsing
# --------------------------------------------------------------------------
PROFILE="release"
BUILD_DIR=""
BUILD_TYPE=""
SANITIZER=""
FAST_ITERATION="OFF"
WANT_TESTS_ONLY=0
ACTION="build"          # build | test | safe | run | rebuild | clean
TEST_FILTER=""
RUN_ARGS=()
USE_CCACHE="auto"
JOBS=""
QT_OVERRIDE="${CHADVIS_QT_PATH:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)     usage; exit 0 ;;
        -f|--fast)     PROFILE="fast" ;;
        -d|--debug)    PROFILE="debug" ;;
        -r|--release)  PROFILE="release" ;;
        release)       PROFILE="release" ;;
        --tsan)        PROFILE="tsan" ;;
        --asan)        PROFILE="asan" ;;
        --ubsan)       PROFILE="ubsan" ;;
        --tests)       WANT_TESTS_ONLY=1 ;;
        --test)        ACTION="test" ;;
        --safe)        ACTION="safe" ;;
        --run)         ACTION="run" ;;
        --rebuild)     ACTION="rebuild" ;;
        -c|--clean|--clean-only) ACTION="clean" ;;
        -j|--jobs)     [[ $# -ge 2 ]] || die "$1 needs a number"; JOBS="$2"; shift ;;
        --no-ccache)   USE_CCACHE="no" ;;
        --ccache)      USE_CCACHE="yes" ;;
        --qt)          [[ $# -ge 2 ]] || die "--qt needs a path"; QT_OVERRIDE="$2"; shift ;;
        build)         ACTION="build" ;;
        test)          ACTION="test" ;;
        run)           ACTION="run" ;;
        clean)         ACTION="clean" ;;
        rebuild)       ACTION="rebuild" ;;
        -*)            printf '%sERROR%s build.sh: unknown option: %q\n' "$RED" "$RESET" "$1" >&2
                       usage >&2; exit 2 ;;
        *)             RUN_ARGS+=("$1") ;;
    esac
    shift
done

# A bare word after --test is that test's name filter.
if [[ "$ACTION" == "test" && ${#RUN_ARGS[@]} -gt 0 ]]; then
    TEST_FILTER="${RUN_ARGS[0]}"
    RUN_ARGS=()
fi

# --------------------------------------------------------------------------
# Profile -> configure arguments
# --------------------------------------------------------------------------
case "$PROFILE" in
    release) BUILD_DIR="build";       BUILD_TYPE="Release" ;;
    fast)    BUILD_DIR="build-fast";  BUILD_TYPE="Debug";  FAST_ITERATION="ON" ;;
    debug)   BUILD_DIR="build-debug"; BUILD_TYPE="Debug" ;;
    tsan)    BUILD_DIR="build-tsan";  BUILD_TYPE="RelWithDebInfo"; SANITIZER="thread" ;;
    asan)    BUILD_DIR="build-asan";  BUILD_TYPE="RelWithDebInfo"; SANITIZER="address" ;;
    ubsan)   BUILD_DIR="build-ubsan"; BUILD_TYPE="RelWithDebInfo"; SANITIZER="undefined" ;;
    *)       die "unknown profile '$PROFILE'" ;;
esac
if [[ -z "$QT_OVERRIDE" ]]; then
    for candidate in /opt/homebrew/opt/qt /usr/local/opt/qt /usr/lib/qt6; do
        if [[ -d "$candidate" ]]; then
            QT_OVERRIDE="$candidate"
            break
        fi
    done
fi

if [[ -z "$JOBS" ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
    [[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -ge 1 ]] || JOBS=4
    # One core back for the desktop the user is actually looking at. Building with
    # every core on a 4-core box is what makes the machine feel broken.
    [[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -gt 2 ]] && JOBS=$((JOBS - 1))
fi

ccache_bin=""
if [[ "$USE_CCACHE" != "no" ]] && command -v ccache >/dev/null 2>&1; then
    ccache_bin="$(command -v ccache)"
elif [[ "$USE_CCACHE" == "yes" ]]; then
    die "--ccache requested but ccache is not on PATH"
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/$BUILD_DIR"

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
archive_build_dir() {
    [[ -d "$BUILD_DIR" ]] || return 0
    local graveyard="$ROOT/.backup_graveyard"
    local stamp destination
    stamp="$(date +%Y%m%d-%H%M%S)"
    destination="$graveyard/$(basename "$BUILD_DIR")-$stamp"
    [[ -e "$destination" ]] && destination="$destination-$$"
    mkdir -p "$graveyard"
    printf '%sARCHIVE%s %s -> %s\n' "$YELLOW" "$RESET" "$BUILD_DIR" "$destination"
    mv "$BUILD_DIR" "$destination"
}

configure_build() {
    local args=(
        -G Ninja
        -S "$ROOT"
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DCHADVIS_FAST_ITERATION="$FAST_ITERATION"
    )
    [[ -n "$SANITIZER" ]] && args+=(-DCHADVIS_SANITIZER="$SANITIZER")
    [[ -n "$QT_OVERRIDE" ]] && args+=(-DCMAKE_PREFIX_PATH="$QT_OVERRIDE")
    # Recorded so sdk_is_stale() can notice the SDK being replaced under us.
    # Deliberately NOT passed as CMAKE_OSX_SYSROOT: adding the SDK the compiler
    # already reports for itself drops /usr/local/include from /usr/bin/c++'s
    # search list (6 entries -> 5), stripping this project's only route to
    # fmt/spdlog/toml++/glm/taglib. That has been measured twice now.
    args+=("-DCHADVIS_SDK_AT_CONFIGURE=$(xcrun --show-sdk-path 2>/dev/null || true)")
    if [[ -n "$ccache_bin" ]]; then
        args+=(-DCMAKE_CXX_COMPILER_LAUNCHER="$ccache_bin")
        args+=(-DCMAKE_C_COMPILER_LAUNCHER="$ccache_bin")
    fi
    printf '%sCONFIGURE%s %s (%s%s)\n' "$CYAN" "$RESET" "$BUILD_DIR" "$BUILD_TYPE" \
        "$( [[ -n "$SANITIZER" ]] && printf ', -fsanitize=%s' "$SANITIZER" )"
    cmake "${args[@]}"
}

ensure_configured() {
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure_build
    fi
}

# A build directory remembers the SDK that existed when it was configured, and
# nothing re-validates it. Measured 2026-10-05: the Release profile stopped
# building entirely because `build/` was configured against
# /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk and updating the command
# line tools replaced CommandLineTools with Xcode-beta, which removed that path.
#
# The symptom is maximally misleading: libc++ headers missing, `map` not found,
# `__stddef_rsize_t.h` not found. That reads like a broken toolchain, and the two
# compile lines differed only in -O3 versus -O0, so it is very easy to start
# debugging the wrong thing. The one real diagnostic is ninja's:
#
#   '/Library/.../MacOSX.sdk/System/.../OpenGL.framework', needed by
#   chadvis-projectm-qt, missing and no known rule to make it
#
# Note that CMAKE_OSX_SYSROOT in the cache is EMPTY even in a build broken this
# way -- the stale SDK reached the link line through find_library(OpenGL) and the
# projectm submodule, not through that variable. So the SDK is recorded
# explicitly at configure time (CHADVIS_SDK_AT_CONFIGURE below) and compared
# against what `xcrun` resolves NOW. Reconfigure rather than nuke: it is seconds
# and it preserves the object files that are still valid.
sdk_is_stale() {
    local recorded resolved
    # The cache TYPE is UNINITIALIZED, not INTERNAL: the variable is not declared
    # in any CMakeLists, it is only ever passed with -D. Match any type rather
    # than guessing one -- the first version of this looked for INTERNAL and so
    # silently matched nothing, which is the failure mode this whole function
    # exists to prevent.
    recorded="$(sed -n 's/^CHADVIS_SDK_AT_CONFIGURE:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")"
    # No recorded SDK means this cache predates the check; treat as fresh rather
    # than reconfiguring a build that might be perfectly good.
    [[ -z "$recorded" ]] && return 1
    resolved="$(xcrun --show-sdk-path 2>/dev/null || true)"
    [[ "$recorded" == "$resolved" ]] && return 1
    if [[ -n "$resolved" && ! -d "$recorded" ]]; then
        printf 'build.sh: the SDK this build was configured against (%s) no longer exists;\n' \
            "$recorded" >&2
        printf 'build.sh: xcrun now resolves %s. Reconfiguring %s.\n' \
            "$resolved" "$BUILD_DIR" >&2
    fi
    return 0
}

do_build() {
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && sdk_is_stale; then
        configure_build
    else
        ensure_configured
    fi
    # macOS ships bash 3.2, where "${arr[@]}" on an *empty* array is an unbound
    # variable under `set -u`. The ${arr[@]+...} guard expands to nothing when the
    # array is empty and to the quoted expansion when it is not, so a plain
    # `cmake --build . ` (no target list) is expressible without tripping -u.
    local target_args=()
    if [[ "$WANT_TESTS_ONLY" -eq 1 ]]; then
        target_args=(--target unit_tests "${SAFE_TESTS[@]}")
    fi
    printf '%sBUILD%s %s (%s jobs%s)\n' "$CYAN" "$RESET" "$BUILD_DIR" "$JOBS" \
        "$( [[ "$WANT_TESTS_ONLY" -eq 1 ]] && printf ', tests only' )"
    cmake --build "$BUILD_DIR" --parallel "$JOBS" \
        ${target_args[@]+"${target_args[@]}"}
}

do_ctest() {
    local args=(--test-dir "$BUILD_DIR/tests" --output-on-failure)
    case "$ACTION" in
        safe)
            # One alternation, not one -R per name: ctest's -R takes a single
            # value, so repeating it silently keeps only the last match and
            # `--safe` would quietly run 1 of its 7 entries.
            local pattern=""
            local t
            for t in "${SAFE_TESTS[@]}"; do
                [[ -n "$pattern" ]] && pattern+="|"
                pattern+="^${t}\$"
            done
            ctest "${args[@]}" -R "^(${pattern})"
            ;;
        test)
            if [[ -n "$TEST_FILTER" ]]; then
                ctest "${args[@]}" -R "$TEST_FILTER"
            else
                ctest "${args[@]}"
            fi
            ;;
    esac
}

do_run() {
    local bin="$BUILD_DIR/tests/unit/unit_tests"
    [[ -x "$bin" ]] || die "no unit_tests binary in $BUILD_DIR -- build it first (./build.sh --tests)"
    printf '%sRUN%s %s %s\n' "$CYAN" "$RESET" "$bin" "${RUN_ARGS[*]-}"
    # -o -,txt keeps QTest's output on stdout; without it a piped invocation can
    # lose the failure text that is the entire point of the run. Same bash 3.2
    # empty-array guard as do_build.
    ( cd "$BUILD_DIR/tests" && exec "$bin" ${RUN_ARGS[@]+"${RUN_ARGS[@]}"} )
}

# --------------------------------------------------------------------------
# Drive
# --------------------------------------------------------------------------
case "$ACTION" in
    clean)
        archive_build_dir
        printf '%sOK%s %s archived\n' "$GREEN" "$RESET" "$(basename "$BUILD_DIR")"
        ;;
    rebuild)
        archive_build_dir
        configure_build
        do_build
        ;;
    build)
        do_build
        ;;
    test|safe)
        do_build
        do_ctest
        ;;
    run)
        do_build
        do_run
        ;;
esac

if [[ "$ACTION" == "build" && "$WANT_TESTS_ONLY" -eq 0 ]]; then
    printf '%sDONE%s %s\n' "$GREEN" "$RESET" "$BUILD_DIR/chadvis-projectm-qt"
fi
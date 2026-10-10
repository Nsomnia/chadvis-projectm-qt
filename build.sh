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
  ${GREEN}(no flags)${RESET}       -O0 -g0 + ccache, ./build-fast  ${YELLOW}default${RESET}: the fastest possible edit/test loop
  ${GREEN}-f, --fast${RESET}       same as no flags
  ${GREEN}-d, --debug${RESET}      Debug with debug info, ./build-debug
  ${GREEN}-r, --release${RESET}    optimized Release, ./build    the shipping configuration ${YELLOW}(explicit)${RESET}
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
  ${GREEN}-j, --jobs N${RESET}     compile jobs (default: ${CPU_PERCENT}% of PHYSICAL cores, capped by ${MEM_PERCENT}% of available RAM)
  ${GREEN}--ctest-jobs N${RESET}  parallel ctest entries (default: 2; the suites are idle ~84% of the time)
  ${GREEN}--nice N${RESET}         nice level for every child (default ${NICE_LEVEL}, ${YELLOW}0 disables${RESET})
  ${GREEN}--no-ccache${RESET}      do not use ccache even if installed
  ${GREEN}--qt PATH${RESET}        Qt6 prefix (else \$CHADVIS_QT_PATH, then Homebrew prefixes)
  ${GREEN}--budget${RESET}         print the resource budget and exit; builds nothing

  ${YELLOW}Environment${RESET}: CHADVIS_CPU_PERCENT, CHADVIS_MEM_PERCENT, CHADVIS_PER_JOB_MIB,
  CHADVIS_CTEST_JOBS, CHADVIS_NICE, CHADVIS_CCACHE_BIN.

${BOLD}Examples${RESET}
  ./build.sh --tests --test             # tight loop: build tests, run the headless-safe suites
  ./build.sh --run aHealthy             # tightest loop: one QTest function, no ctest
  ./build.sh                            # default fast build, everything
  ./build.sh --release                  # optimized Release, everything, for a real check
  ./build.sh --release --test unit_tests # Release tests only, ctest all entries
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
# Default profile is `fast`: -O0 -g0 in build-fast/ with ccache. An optimized
# build is the explicit opt-in (--release), not the thing you get by accident
# when you just wanted the edit/test loop to turn around quickly.
PROFILE="fast"
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
JOBS_EXPLICIT=0
CTEST_JOBS=""
# Defaults live HERE, before the parser, not in the resource-budget section
# below. They were not, and `--nice 5` was silently discarded: the parser set
# it, and then this section ran afterwards and overwrote it with the env
# default. A flag that parses, validates, and then does nothing is the worst
# of the three failure modes -- it looks like it worked.
NICE_LEVEL="${CHADVIS_NICE:-10}"
# These three are interpolated into --help, which runs INSIDE the option parser
# and exits. Anything the help text mentions must therefore exist before the
# loop, not after it -- otherwise `./build.sh --help` dies on `set -u` before it
# prints a line, which is exactly the "help is broken" bug nobody reports.
CPU_PERCENT="${CHADVIS_CPU_PERCENT:-75}"
MEM_PERCENT="${CHADVIS_MEM_PERCENT:-75}"
PER_JOB_MIB="${CHADVIS_PER_JOB_MIB:-512}"
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
        --ctest-jobs)  [[ $# -ge 2 ]] || die "$1 needs a number"; CTEST_JOBS="$2"; shift ;;
        --nice)        [[ $# -ge 2 ]] || die "--nice needs a number"; NICE_LEVEL="$2"; shift ;;
        --no-nice)     NICE_LEVEL="0" ;;
        --budget)      ACTION="budget" ;;
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

# --------------------------------------------------------------------------
# Resource budget
# --------------------------------------------------------------------------
# A build that saturates the machine is not "slower", it is unusable: the
# pointer stops tracking, the window stops painting, and a machine already
# paging will fall off a cliff. So this script spends a fixed FRACTION of the
# box rather than all of it, and the fraction is overridable rather than baked
# in, because a CI runner and a laptop are not the same machine.
#
# The numbers below are MEASURED on the reference box (macOS 14.8, i5-8210Y,
# 2 physical cores / 4 hyperthreads, 8 GB) by pulling real commands out of
# `ninja -t commands` and running each under /usr/bin/time -l:
#
#   compile, largest TU (src/qml_bridge/SunoBridge.cpp)
#     -O0 -g0                            306 MiB peak RSS
#     -O3 -DNDEBUG                       301 MiB
#     RelWithDebInfo -fsanitize=thread   338 MiB
#   link, tests/unit/unit_tests
#     (36 objects + Qt frameworks + projectM static)  160 MiB
#   unit_tests, full run                114 MiB, and 14 s of CPU across a 91 s
#                                       wall clock -- it is timers, not compute
#
# Two things follow from that table, and they are the whole design:
#
#   * A compile job is budgeted at 512 MiB -- the measured worst case plus room
#     for a heavier TU. It is deliberately NOT scaled per profile: the sanitizer
#     cost lands at RUN time, not compile time, and that cap lives on the ctest
#     side. Guessing a per-profile number here would be a fiction.
#   * The test side is nearly free in CPU (14 s across 91 s), so it CAN be
#     parallel where compiles cannot. That asymmetry is why CTEST_JOBS is not
#     simply JOBS.
#
# `inxi` is deliberately not used: it reports memory as N/A on macOS (verified,
# it wants dmidecode) and this is a macOS-first script. sysctl/vm_stat on
# Darwin and /proc on Linux cover both with no dependency to install.
# CPU_PERCENT / MEM_PERCENT / PER_JOB_MIB are declared with the other defaults
# above, because --help quotes them and runs before this section.

OS_NAME="$(uname -s 2>/dev/null || echo unknown)"

detect_physical_cores() {
    local n=""
    case "$OS_NAME" in
        Darwin)
            n="$(sysctl -n hw.physicalcpu 2>/dev/null || true)"
            ;;
        Linux)
            # /proc/cpuinfo lists LOGICAL cpus. The ones that actually compete
            # for a core are the unique (physical id, core id) pairs -- this
            # matters exactly as much as it sounds on a hyperthreaded laptop,
            # where 4 logical cpus are 2 cores' worth of throughput.
            n="$(awk '/^physical id/ {p=$4} /^core id/ {print p":"$4}' \
                    /proc/cpuinfo 2>/dev/null | sort -u | wc -l | tr -d ' ')"
            ;;
    esac
    if ! [[ "$n" =~ ^[0-9]+$ ]] || [[ "$n" -lt 1 ]]; then
        n="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
    fi
    if ! [[ "$n" =~ ^[0-9]+$ ]] || [[ "$n" -lt 1 ]]; then n=4; fi
    printf '%s' "$n"
}

detect_total_ram_mib() {
    local bytes=""
    case "$OS_NAME" in
        Darwin) bytes="$(sysctl -n hw.memsize 2>/dev/null || true)" ;;
        Linux)  bytes="$(awk '/^MemTotal:/ {print $2 * 1024}' /proc/meminfo 2>/dev/null)" ;;
    esac
    if ! [[ "$bytes" =~ ^[0-9]+$ ]] || [[ "$bytes" -lt 1 ]]; then return 1; fi
    printf '%s' "$((bytes / 1048576))"
}

# Total RAM answers "can this machine ever hold N jobs". It does NOT answer "can
# I hold N jobs right now" -- the user's browser and the other four logged-in
# sessions already own most of it. Available is the number that decides whether
# a build swaps, and swapping is the actual "potato crawling" failure mode.
detect_available_ram_mib() {
    local page="" avail=""
    case "$OS_NAME" in
        Darwin)
            page="$(sysctl -n hw.pagesize 2>/dev/null || echo 4096)"
            # free + inactive + speculative + purgeable. `inactive` is largely
            # reclaimable file cache, so counting it is what makes this an
            # "available" figure rather than a "free" one.
            #
            # $NF, not $2: vm_stat separates the label from the value with
            # SPACES ("Pages free:   20965."), so a colon split reads nothing
            # and silently returns empty -- which is exactly the kind of bug
            # that looks like "the machine really does have 8 GB free".
            avail="$(vm_stat 2>/dev/null | awk '
                /Pages free/        {f=$NF} /Pages inactive/    {i=$NF}
                /Pages speculative/ {s=$NF} /Pages purgeable/   {p=$NF}
                END {gsub(/[^0-9]/,"",f); gsub(/[^0-9]/,"",i);
                     gsub(/[^0-9]/,"",s); gsub(/[^0-9]/,"",p);
                     print f+i+s+p}')"
            if [[ "$avail" =~ ^[0-9]+$ ]] && [[ "$avail" -gt 0 ]]; then
                avail=$((avail * page / 1048576))
            else
                avail=""
            fi
            ;;
        Linux)
            avail="$(awk '/^MemAvailable:/ {print int($2/1024)}' /proc/meminfo 2>/dev/null)"
            ;;
    esac
    if ! [[ "$avail" =~ ^[0-9]+$ ]] || [[ "$avail" -lt 1 ]]; then
        detect_total_ram_mib || return 1
        return 0
    fi
    printf '%s' "$avail"
}

compute_resource_budget() {
    local phys avail total
    phys="$(detect_physical_cores)"
    total="$(detect_total_ram_mib 2>/dev/null || echo 0)"
    avail="$(detect_available_ram_mib 2>/dev/null || echo 0)"

    # ceil(phys * pct / 100) with integer arithmetic: no float, no awk, and it
    # cannot land on 1.5 jobs.
    BUDGET_JOBS_CPU=$(( (phys * CPU_PERCENT + 99) / 100 ))
    # ...but never fewer jobs than the box has cores. At 75% of 2 cores the
    # arithmetic says 1, and a single-job build is not "marginally slower", it
    # is a different build. The floor is what keeps the promise honest.
    if [[ "$BUDGET_JOBS_CPU" -lt "$phys" ]]; then BUDGET_JOBS_CPU="$phys"; fi

    BUDGET_USABLE_MIB=$(( avail * MEM_PERCENT / 100 ))
    BUDGET_JOBS_MEM=$(( BUDGET_USABLE_MIB / PER_JOB_MIB ))
    if [[ "$BUDGET_JOBS_MEM" -lt 1 ]]; then BUDGET_JOBS_MEM=1; fi

    BUDGET_JOBS="$BUDGET_JOBS_CPU"
    BUDGET_BINDER="cpu"
    if [[ "$BUDGET_JOBS_MEM" -lt "$BUDGET_JOBS_CPU" ]]; then
        BUDGET_JOBS="$BUDGET_JOBS_MEM"
        BUDGET_BINDER="memory"
    fi
    BUDGET_PHYS="$phys"
    BUDGET_TOTAL_MIB="$total"
    BUDGET_AVAIL_MIB="$avail"
    if [[ "$BUDGET_AVAIL_MIB" -gt 0 && "$BUDGET_JOBS_MEM" -le 1 ]]; then
        BUDGET_BINDER="memory-starved"
    fi
}

compute_resource_budget

if [[ -z "$JOBS" ]]; then
    JOBS="$BUDGET_JOBS"
else
    JOBS_EXPLICIT=1
    if [[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -ge 1 ]]; then
        if [[ "$JOBS" -gt "$BUDGET_JOBS" ]]; then
            warn "-j $JOBS overrides the budget of $BUDGET_JOBS (${BUDGET_PHYS} cores, ${BUDGET_AVAIL_MIB} MiB available). That is your call, not the default."
        fi
    else
        die "--jobs wants a positive integer, got '$JOBS'"
    fi
fi

# ctest parallelism is budgeted separately and deliberately: measured, the test
# binaries are idle 84% of the time (14 s CPU across a 91 s run), so they get
# parallelism the compiles cannot, but still a fraction of the machine.
CTEST_JOBS="${CTEST_JOBS:-${CHADVIS_CTEST_JOBS:-}}"
if [[ -z "$CTEST_JOBS" ]]; then
    CTEST_JOBS=$(( JOBS > 2 ? 2 : JOBS ))
fi
[[ "$CTEST_JOBS" =~ ^[0-9]+$ && "$CTEST_JOBS" -ge 1 ]] || die "--ctest-jobs wants a positive integer, got '$CTEST_JOBS'"

# ccache first, sccache second. cmake/Compiler.cmake already probes both in
# that order; the script used to only look for ccache, so a machine with only
# sccache installed silently built uncached.
ccache_bin="${CHADVIS_CCACHE_BIN:-}"
if [[ -z "$ccache_bin" && "$USE_CCACHE" != "no" ]]; then
    for candidate in ccache sccache; do
        if command -v "$candidate" >/dev/null 2>&1; then
            ccache_bin="$(command -v "$candidate")"
            break
        fi
    done
fi
if [[ -z "$ccache_bin" && "$USE_CCACHE" == "yes" ]]; then
    die "--ccache requested but neither ccache nor sccache is on PATH"
fi

# Nice the whole build rather than asking the user to remember to. The child
# compilers and linkers inherit it, so one nice buys the desktop the whole tree.
NICE_CMD=()
if [[ "$NICE_LEVEL" != "0" ]] && command -v nice >/dev/null 2>&1; then
    NICE_CMD=(nice -n "$NICE_LEVEL")
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

# A build directory remembers the toolchain that generated it, and nothing
# re-validates either half of that memory.
#
# Half one is the SDK. Measured 2026-10-05: the Release profile stopped
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
#
# Half two is CMake itself, and it bites harder. build.ninja hardcodes the
# absolute cmake in every command that regenerates it, so when the machine
# migrates its package manager -- Homebrew's /usr/local/bin/cmake replaced by
# MacPorts' /opt/local/bin/cmake -- every build directory in the tree becomes
# unbuildable at once:
#
#   /bin/sh: /usr/local/bin/cmake: No such file or directory
#   FAILED: [code=127] build.ninja ...
#   ninja: error: rebuilding 'build.ninja': subcommand failed
#
# which reads like a broken install and is not one: cmake is on PATH and works.
#
# The probe reads build.ninja, NOT CMakeCache.txt, and that distinction is not
# a detail. A failed reconfigure rewrites CMakeCache.txt's CMAKE_COMMAND before
# it errors out, which leaves the cache claiming a cmake that exists while the
# 166 commands in build.ninja still name the one that does not. A cache-based
# check calls that directory healthy and then trips over the original error
# forever. build.ninja is the file ninja actually executes, so it is the file
# worth believing.
#
# Existence only, never equality with the cmake on PATH: two co-installed
# versions must not be able to ping-pong a reconfigure loop.
cmake_is_stale() {
    [[ -f "$BUILD_DIR/build.ninja" ]] || return 1
    local recorded
    # The regeneration command is
    #   COMMAND = cd <build> && <cmake> --regenerate-during-build -S.. -B..
    # so the token in front of --regenerate-during-build is the binary ninja
    # will actually execute. Anchoring on that flag matters: build.ninja also
    # names cmake in an unrelated deploy rule, and a looser match would call a
    # healthy directory stale (or a stale one healthy) for the wrong reason.
    recorded="$(sed -n 's/.*&& \([^ ]*\) --regenerate-during-build.*/\1/p' \
                   "$BUILD_DIR/build.ninja" | head -1)"
    if [[ -z "$recorded" ]]; then
        # Fall back to the cache for a generator whose build.ninja does not put
        # cmake in a `command =` line.
        recorded="$(sed -n 's/^CMAKE_COMMAND:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")"
    fi
    [[ -z "$recorded" ]] && return 1
    [[ -x "$recorded" ]] && return 1
    printf 'build.sh: %s is generated by %s, which is no longer installed.\n' \
        "$BUILD_DIR/build.ninja" "$recorded" >&2
    printf 'build.sh: cmake on PATH is %s. Reconfiguring rather than discarding the objects.\n' \
        "$(command -v cmake 2>/dev/null || echo '<not found>')" >&2
    return 0
}
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
    # Reconfigure when the machine changed under this build directory, for any
    # of the reasons the two staleness checks know about. Reconfigure, never
    # nuke: it costs seconds and keeps every object that is still valid.
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && { sdk_is_stale || cmake_is_stale; }; then
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
    printf '%sBUDGET%s %s of %s physical cores, %s MiB available of %s total, %s MiB/job, bound: %s%s\n' \
        "$YELLOW" "$RESET" "$JOBS" "$BUDGET_PHYS" "$BUDGET_AVAIL_MIB" \
        "${BUDGET_TOTAL_MIB:-?}" "$PER_JOB_MIB" "$BUDGET_BINDER" \
        "$( [[ "$JOBS_EXPLICIT" -eq 1 ]] && printf ' (--jobs override)' )"
    if [[ "$BUDGET_BINDER" == "memory-starved" ]]; then
        warn "only $(( BUDGET_AVAIL_MIB * MEM_PERCENT / 100 )) MiB of the ${BUDGET_AVAIL_MIB} MiB available is budgeted at ${MEM_PERCENT}% -- close other apps, or this build will page."
    fi
    ${NICE_CMD[@]+"${NICE_CMD[@]}"} cmake --build "$BUILD_DIR" --parallel "$JOBS" \
        ${target_args[@]+"${target_args[@]}"}
}

do_ctest() {
    # -j is bounded separately from the build. Measured: unit_tests burns 14 s
    # of CPU across a 91 s wall clock, so test binaries are cheap to run
    # concurrently in a way compilers are not. Serial was the old default only
    # because nothing had measured that yet.
    local args=(--test-dir "$BUILD_DIR/tests" --output-on-failure -j "$CTEST_JOBS")
    printf '%sCTEST%s %s at -j%s%s\n' "$CYAN" "$RESET" "$BUILD_DIR/tests" "$CTEST_JOBS" \
        "$( [[ -n "$TEST_FILTER" ]] && printf ', filtered to %s' "$TEST_FILTER" )"
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
    ( cd "$BUILD_DIR/tests" && exec ${NICE_CMD[@]+"${NICE_CMD[@]}"} "$bin" ${RUN_ARGS[@]+"${RUN_ARGS[@]}"} )
}

# --------------------------------------------------------------------------
# Drive
# --------------------------------------------------------------------------
case "$ACTION" in
    budget)
        # Exists so the budget can be inspected -- and taught -- without paying
        # for a build. An agent tuning this should never need a 12-minute
        # compile to find out what the box thinks it can afford.
        printf '%sBUDGET%s on %s\n' "$CYAN" "$RESET" "$OS_NAME"
        printf '  physical cores        %s   (logical: %s)\n' "$BUDGET_PHYS" \
            "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo '?')"
        printf '  memory                %s MiB available of %s MiB total\n' \
            "$BUDGET_AVAIL_MIB" "${BUDGET_TOTAL_MIB:-?}"
        printf '  fractions             cpu %s%%, memory %s%%, %s MiB per job\n' \
            "$CPU_PERCENT" "$MEM_PERCENT" "$PER_JOB_MIB"
        printf '  cpu allows            %s jobs%s\n' "$BUDGET_JOBS_CPU" \
            "$( [[ "$BUDGET_JOBS_CPU" -eq "$BUDGET_PHYS" ]] && printf ' (floored at the physical core count)' )"
        printf '  memory allows         %s jobs (%s MiB budgeted)\n' \
            "$BUDGET_JOBS_MEM" "$BUDGET_USABLE_MIB"
        printf '  chosen                %s jobs, bound by %s\n' "$JOBS" "$BUDGET_BINDER"
        printf '  ctest                 %s entries at -j%s\n' "$BUILD_DIR/tests" "$CTEST_JOBS"
        printf '  niceness              %s\n' \
            "$( [[ "$NICE_LEVEL" == "0" ]] && echo 'off' || echo "$NICE_LEVEL" )"
        printf '  compiler cache        %s\n' \
            "$( [[ -n "$ccache_bin" ]] && basename "$ccache_bin" || echo 'none' )"
        ;;
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
    # The macOS target is a real MACOSX_BUNDLE as of 2026-10-05, so the binary
    # lives inside the .app rather than at the top of the build dir. Print the
    # path that actually exists instead of the one that used to.
    BUNDLED="$BUILD_DIR/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt"
    if [[ -x "$BUNDLED" ]]; then
        printf '%sDONE%s %s\n' "$GREEN" "$RESET" "$BUNDLED"
    else
        printf '%sDONE%s %s\n' "$GREEN" "$RESET" "$BUILD_DIR/chadvis-projectm-qt"
    fi
fi
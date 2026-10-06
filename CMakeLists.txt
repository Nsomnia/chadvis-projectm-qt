cmake_minimum_required(VERSION 3.20)

# ChadVis - Qt6 projectM v4 visualizer in modern C++23.
#
# This root file is a slim orchestrator. All real logic lives in modules
# under cmake/, included in dependency order:
#
#   cmake/Compiler.cmake     C++23 standard, toolchain floor, warning/opt flags
#   cmake/Dependencies.cmake Qt6, CPM packages, pkg-config deps + fallbacks, projectM
#   cmake/FindProjectM4.cmake projectM v4 detection (config -> pkg-config -> CPM)
#   cmake/Sources.cmake      per-module source and QML file lists
#   cmake/TargetSetup.cmake  project_lib static library, QML module, executable
#   cmake/Install.cmake      install rules and CPack configuration

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" CHADVIS_VERSION)
string(STRIP "${CHADVIS_VERSION}" CHADVIS_VERSION)
if(NOT CHADVIS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt must contain a numeric semantic version")
endif()
project(chadvis-projectm-qt VERSION ${CHADVIS_VERSION} LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Standard settings
# ---------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Default to ReleaseWithDebInfo, not Debug and not plain Release.
#
# Debug defaults were defensible while the app had never been launched and every
# cycle was a rebuild; they are indefensible for a configuration a stranger will
# run. Release drops the symbols, so a crash on a user's machine is a backtrace
# of addresses with no line numbers, no function names and nothing to hand to a
# profiler. RelWithDebInfo keeps DWARF while still optimizing, which is the
# configuration a distributed build actually wants: -O2 -g -DNDEBUG from
# CMake's own defaults, NDEBUG for speed, symbols for the crash report.
#
# An explicit -DCMAKE_BUILD_TYPE=... still wins. The FORCE below only runs
# inside `if(NOT CMAKE_BUILD_TYPE)`, so this cannot overwrite a value the
# command line supplied -- build.sh passes -DCMAKE_BUILD_TYPE on every profile
# and is therefore unaffected by this change.
#
# CMAKE_CONFIGURATION_TYPES is also checked: multi-config generators (Xcode,
# Visual Studio) leave CMAKE_BUILD_TYPE empty by design and select the
# configuration at build time, so forcing a value there would put a misleading
# single-config answer in the cache.
#
# One honest interaction to know about: cmake/Compiler.cmake adds -O3 through a
# $<CONFIG:Release> generator expression, so RelWithDebInfo does not get -O3 --
# it gets CMake's own -O2 -g -DNDEBUG. That is still an optimized binary, not
# an -O0 one, which is the point of the change. Anyone who wants -O3 with
# symbols should say so explicitly rather than have two files each claim to own
# the optimization level:
#     -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -DNDEBUG"
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE ReleaseWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# Keep Qt automatic features OFF globally so dependency targets (pffft, etc.)
# are never moc'd/scanned; project_lib re-enables them explicitly.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------

include(Compiler)
include(Dependencies)
include(Sources)
include(TargetSetup)
include(Install)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

add_subdirectory(tests)

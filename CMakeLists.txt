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

project(chadvis-projectm-qt VERSION 1.0.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Standard settings
# ---------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Default to Debug for rapid development iterations; override with
# -DCMAKE_BUILD_TYPE=Release for distributable builds.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
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

# Dependencies.cmake - Third-party dependency discovery.
#
# Strategy:
#   * Qt6 is a hard requirement via find_package (CONFIG mode).
#   * Header-first libraries (spdlog, fmt, tomlplusplus) use the system
#     package when available, falling back to CPM source builds.
#   * pkg-config is used opportunistically for taglib/FFmpeg/projectM, with
#     find_path/find_library fallbacks so vcpkg-style layouts work without
#     pkg-config (e.g. Windows).
#   * projectM v4 detection lives in FindProjectM4.cmake.

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Multimedia Network Quick Qml QuickControls2 Sql)

# pkg-config is optional: everything below degrades to manual search.
find_package(PkgConfig QUIET)

# OpenGL - used directly by the visualizer renderers. The bare "OpenGL"
# link name only worked by accident on Linux; OpenGL::GL is portable.
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# CPM.cmake bootstrap (downloaded at configure time, cached locally)
# ---------------------------------------------------------------------------

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/CPM.cmake")
    file(DOWNLOAD
         https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.0/CPM.cmake
         ${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

# ---------------------------------------------------------------------------
# Header-only / small libraries: system first, CPM fallback
# ---------------------------------------------------------------------------

# spdlog - logging
find_package(spdlog CONFIG QUIET)
if(TARGET spdlog::spdlog)
    message(STATUS "Using system spdlog")
else()
    CPMAddPackage(
        NAME spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        VERSION 1.14.1
        OPTIONS "SPDLOG_BUILD_SHARED OFF" "SPDLOG_FMT_EXTERNAL ON"
    )
endif()

# fmt - string formatting
find_package(fmt CONFIG QUIET)
if(TARGET fmt::fmt)
    message(STATUS "Using system fmt")
else()
    CPMAddPackage(
        NAME fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        VERSION 11.0.2
        OPTIONS "FMT_INSTALL OFF"
    )
endif()

# toml++ - config parsing
find_package(tomlplusplus CONFIG QUIET)
if(TARGET tomlplusplus::tomlplusplus)
    message(STATUS "Using system tomlplusplus")
else()
    CPMAddPackage(
        NAME tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        VERSION 3.4.0
    )
endif()

# PFFFT - high-performance SIMD FFT
CPMAddPackage(
    NAME pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG master
    OPTIONS
        "PFFFT_BUILD_TESTS OFF"
        "PFFFT_BUILD_BENCHMARKS OFF"
)

# moodycamel::ReaderWriterQueue - lock-free SPSC queue for audio
CPMAddPackage(
  NAME readerwriterqueue
  GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
  GIT_TAG v1.0.6
)
if(readerwriterqueue_ADDED)
  message(STATUS "Using CPM readerwriterqueue (header-only)")
endif()

# ---------------------------------------------------------------------------
# System libraries: pkg-config first, find_path/find_library fallback
# ---------------------------------------------------------------------------

# TagLib - audio metadata
set(TAGLIB_LIBRARY_NAMES tag)
set(TAGLIB_INCLUDE_HINT taglib/tag.h)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(TAGLIB taglib)
endif()
if(TAGLIB_FOUND)
    message(STATUS "TagLib found via pkg-config (${TAGLIB_VERSION})")
else()
    message(STATUS "TagLib: trying manual search (pkg-config unavailable or no match)")
    find_path(TAGLIB_INCLUDE_DIRS ${TAGLIB_INCLUDE_HINT})
    find_library(TAGLIB_LIBRARIES NAMES ${TAGLIB_LIBRARY_NAMES})
    if(NOT TAGLIB_INCLUDE_DIRS OR NOT TAGLIB_LIBRARIES)
        message(FATAL_ERROR
            "TagLib not found. Install taglib (with development headers) "
            "or point CMake at a prefix containing it.")
    endif()
    message(STATUS "TagLib found via manual search: ${TAGLIB_LIBRARIES}")
endif()

# glm - math library (config package on Linux/macOS/Homebrew/vcpkg)
find_package(glm REQUIRED)

# FFmpeg - libavcodec libavformat libavutil libswscale libswresample
set(CHADVIS_FFMPEG_COMPONENTS avcodec avformat avutil swscale swresample)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG
        libavcodec libavformat libavutil libswscale libswresample)
endif()
if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found via pkg-config (${FFMPEG_VERSION})")
else()
    message(STATUS "FFmpeg: trying per-component manual search")
    foreach(_comp IN LISTS CHADVIS_FFMPEG_COMPONENTS)
        string(TOUPPER "${_comp}" _comp_uc)
        find_path(FFMPEG_${_comp_uc}_INCLUDE_DIR
            lib${_comp}/${_comp}.h)
        find_library(FFMPEG_${_comp_uc}_LIBRARY
            NAMES ${_comp})
        if(NOT FFMPEG_${_comp_uc}_INCLUDE_DIR OR NOT FFMPEG_${_comp_uc}_LIBRARY)
            message(FATAL_ERROR
                "FFmpeg component '${_comp}' not found. Install ffmpeg "
                "development packages or ensure the import libraries are "
                "on CMake's search path (e.g. via vcpkg toolchain file).")
        endif()
        list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${_comp_uc}_INCLUDE_DIR})
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${_comp_uc}_LIBRARY})
    endforeach()
    unset(_comp)
    unset(_comp_uc)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
    message(STATUS "FFmpeg found via manual search: ${FFMPEG_LIBRARIES}")
endif()

# ---------------------------------------------------------------------------
# projectM v4 (system detection with CPM source-build fallback)
# ---------------------------------------------------------------------------

include(FindProjectM4)

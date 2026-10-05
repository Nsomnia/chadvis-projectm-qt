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
    Core Gui Multimedia Network Quick Qml QuickControls2 Sql DBus)

# Qt 6.7 floor, and not for a new feature. `QNetworkRequest::setTransferTimeout`
# arrived in 6.7, and on anything older the call does not exist -- so without the
# floor an optional network-hardening feature silently vanishes on an older Qt
# rather than failing loudly. `cmake/TargetSetup.cmake`'s HttpPolicy wiring
# carries the matching `static_assert(QT_VERSION >= 6.7)` so both halves agree.
if(Qt6_VERSION VERSION_LESS 6.7)
    message(FATAL_ERROR
        "ChadVis requires Qt 6.7 or newer: QNetworkRequest::setTransferTimeout, "
        "which src/suno/HttpPolicy.cpp stamps onto every request, was added in "
        "6.7. On an older Qt the request policy would silently not exist.")
endif()

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

# NOTE: pffft (SIMD FFT) was removed 2026-10-04. Its only consumer was
# src/audio/AudioAnalyzer, which had no caller anywhere in the tree and was
# retired in e058eab. Two things went with it, and both are worth recording:
#
#   * pffft was the one dependency here that BYPASSED the system-first
#     find_package-then-CPM-fallback policy every header-only dep above
#     follows -- it called CPMAddPackage unconditionally. So its removal also
#     removes the last configure-time network fetch for a library that was
#     linked and never called. readerwriterqueue below is now the only
#     remaining unconditional CPMAddPackage, and it is header-only.
#   * It contributed -DPFFFT_STATIC_DEFINE and its include dir to EVERY
#     project TU, transitively through the PFFFT::PFFFT target rather than
#     from an explicit target_compile_definitions/CMakeLists line -- which is
#     why neither string appeared in cmake/ but both appeared in
#     compile_commands.json. Verified: src/audio/AudioEngine.cpp's compile
#     line carried exactly those two extra tokens and nothing else pffft-related.

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
# Post-processing (burn-in) -- OPTIONAL
# ---------------------------------------------------------------------------
#
# libavfilter is probed separately and is deliberately NOT appended to
# CHADVIS_FFMPEG_COMPONENTS. That list FATAL_ERRORs on a missing component, so
# making avfilter mandatory would convert every FFmpeg-4-era container and every
# stripped distro into a build failure of the *entire application* -- including
# the Suno client, which has nothing to do with video. An optional feature that
# can break the build of an unrelated feature is not optional.
#
# Two gates, not one, because they fail independently:
#
#   CHADVIS_HAS_AVFILTER  configure-time. Is the library there? Gates the source
#                         file and the compile definition, so a build without it
#                         compiles, links and runs, and burnInAvailable() reports
#                         itself unsupported at runtime.
#   avfilter_get_by_name   run-time, inside SubtitleBurnIn. A libavfilter built
#                         without libass has no `ass` or `subtitles` filter even
#                         though the library links perfectly. This is the case a
#                         configure-time-only check misses, and the one a distro
#                     shipping a minimal libavfilter is most likely to hit.
#
# libass is NOT looked for separately: it is a dependency of the *filter*, not of
# this project, and libavfilter links it itself. Measured on this machine:
# `otool -L libavfilter.12.dylib` lists libass.9, libfreetype.6 and
# libfontconfig.1, while `libavcodec.63.dylib` links none of them. So the only
# thing to find is libavfilter.

option(CHADVIS_POSTPROCESS
    "FFmpeg post-pass: burn a subtitle track into the pixels of a finished file"
    ON)

set(CHADVIS_HAS_AVFILTER OFF)
if(CHADVIS_POSTPROCESS)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(AVFILTER QUIET libavfilter)
    endif()
    if(AVFILTER_FOUND)
        set(CHADVIS_HAS_AVFILTER ON)
        list(APPEND FFMPEG_INCLUDE_DIRS ${AVFILTER_INCLUDE_DIRS})
        list(APPEND FFMPEG_LIBRARIES ${AVFILTER_LIBRARIES})
        # pkg-config can name a prefix that is not on the default search path.
        # The FFMPEG block above has the same latent gap; this one is closed.
        if(AVFILTER_LIBRARY_DIRS)
            link_directories(${AVFILTER_LIBRARY_DIRS})
        endif()
        message(STATUS
            "Post-pass: libavfilter ${AVFILTER_VERSION} found -- burn-in enabled")
    else()
        # Non-fatal manual fallback, mirroring the loop above.
        find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h)
        find_library(AVFILTER_LIBRARY NAMES avfilter)
        if(AVFILTER_INCLUDE_DIR AND AVFILTER_LIBRARY)
            set(CHADVIS_HAS_AVFILTER ON)
            list(APPEND FFMPEG_INCLUDE_DIRS ${AVFILTER_INCLUDE_DIR})
            list(APPEND FFMPEG_LIBRARIES ${AVFILTER_LIBRARY})
            message(STATUS
                "Post-pass: libavfilter found via manual search (${AVFILTER_LIBRARY})"
                " -- burn-in enabled")
        else()
            message(STATUS
                "Post-pass: libavfilter NOT found. CHADVIS_POSTPROCESS is ON but "
                "burn-in will report itself unsupported at runtime. This is not a "
                "build error; every other feature is unaffected. Install "
                "libavfilter, or pass -DCHADVIS_POSTPROCESS=OFF to skip the probe.")
        endif()
    endif()
else()
    message(STATUS
        "Post-pass: disabled by CHADVIS_POSTPROCESS=OFF -- burn-in compiled out")
endif()


# ---------------------------------------------------------------------------
# projectM v4 (system detection with CPM source-build fallback)
# ---------------------------------------------------------------------------

include(FindProjectM4)

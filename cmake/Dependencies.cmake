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

# Qt ships its own FFmpeg for its multimedia backend, under <qt>/lib. With the
# qt.io SDK installed the Qt prefix IS on CMAKE_PREFIX_PATH, so the manual
# find_library() below resolves libavcodec there and pairs this tree's
# /usr/local headers with Qt's libraries -- a version skew the linker accepts
# and the runtime does not. Measured 2026-10-23 in a fresh build directory:
# FFMPEG_AVCODEC_INCLUDE_DIR=/usr/local/include with
# FFMPEG_AVCODEC_LIBRARY=~/Qt/6.12.0/macos/lib/libavcodec.dylib.
#
# A prefix that carries Qt's own CMake config is a Qt installation, not an
# FFmpeg provider. Collected here, applied only around the libav* manual search
# below -- excluding it globally would also hide the prefix from the test tree's
# own find_package(Qt6 ...). CMAKE_IGNORE_PREFIX_PATH is CMake 3.23+; on older
# CMake the ambiguity stays possible and this says nothing rather than guessing.
set(_chadvis_qt_prefixes "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        if(EXISTS "${_prefix}/lib/cmake/Qt6/Qt6Config.cmake")
            list(APPEND _chadvis_qt_prefixes "${_prefix}")
        endif()
    endforeach()
endif()

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

# fmt - string formatting (must come before spdlog: spdlog is built with
# SPDLOG_FMT_EXTERNAL ON, so the fmt::fmt target must already exist when
# spdlog's CMake runs find_package(fmt)).
find_package(fmt CONFIG QUIET)
if(TARGET fmt::fmt)
    message(STATUS "Using system fmt")
else()
    CPMAddPackage(
        NAME fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        # 11.0.2, pinned to the commit the tag names rather than to a mutable tag.
        GIT_TAG 0c9fce2ffefecfdce794e1859584e25877b7b592
        OPTIONS "FMT_INSTALL OFF"
    )
endif()

# spdlog - logging
find_package(spdlog CONFIG QUIET)
if(TARGET spdlog::spdlog)
    message(STATUS "Using system spdlog")
else()
    CPMAddPackage(
        NAME spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        # v1.14.1, pinned to the commit the tag names.
        GIT_TAG 27cb4c76708608465c413f6d0e6b8d99a4d84302
        OPTIONS "SPDLOG_BUILD_SHARED OFF" "SPDLOG_FMT_EXTERNAL ON"
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
        # v3.4.0, pinned to the commit the tag names.
        GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de
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
  # v1.0.6, pinned to the commit the tag names. CPM_SHALLOW stays off: a short
  # hash cannot be fetched from a shallow clone.
  GIT_TAG 8e7627d18c2108aca178888d88514179899a044f
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
    # pkg_check_modules reports bare names in TAGLIB_LIBRARIES, so a keg-only
    # prefix (Homebrew /opt/homebrew/opt/taglib, /opt/local) resolves to
    # `-ltag` with nothing on the search path -- `ld: library 'tag' not found`.
    if(TAGLIB_LIBRARY_DIRS)
        link_directories(${TAGLIB_LIBRARY_DIRS})
    endif()
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
    # Same bare-name gap as TagLib above.
    if(FFMPEG_LIBRARY_DIRS)
        link_directories(${FFMPEG_LIBRARY_DIRS})
    endif()
else()
    message(STATUS "FFmpeg: trying per-component manual search")
    # Scope the Qt-prefix exclusion to this search and nothing else: a later
    # find_package(Qt6 ...) in the test tree must still resolve through the
    # prefix it shares. See the block above the TagLib probe for the measurement
    # that made this necessary.
    set(_chadvis_ignore_before "${CMAKE_IGNORE_PREFIX_PATH}")
    list(APPEND CMAKE_IGNORE_PREFIX_PATH ${_chadvis_qt_prefixes})
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
    set(CMAKE_IGNORE_PREFIX_PATH "${_chadvis_ignore_before}")
    unset(_chadvis_ignore_before)
    unset(_comp)
    unset(_comp_uc)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
    message(STATUS "FFmpeg found via manual search: ${FFMPEG_LIBRARIES}")
endif()

# ---------------------------------------------------------------------------
# Encrypted ("mango") playback -- needs libavutil's AES
# ---------------------------------------------------------------------------
#
# The encrypted media channel is decrypted with AES-GCM (the license) and
# AES-CTR (the body). Neither AES mode is in Qt, and OpenSSL would be a new
# native dependency on three platforms plus a vcpkg port and a DLL to ship --
# so the AES block cipher comes from libavutil, which is already required.
#
# The probe is a COMPILE check, not a version check, because libavutil's AES
# entry points changed shape: `av_aes_init()` took the key from FFmpeg 5.1
# onwards and did not before, and `av_aes_crypt()` grew an IV and a direction
# parameter. A version test would have to know which of those the tree
# supports; compiling the exact call does not.
#
# Only the ENCRYPTION direction is used (GCM's keystream is E(K, counter), its
# tag seed is E(K, J0), and CTR's keystream is E(K, counter)), so the probe
# asks for exactly that and nothing else.
# NOTE: the variable is NOT pre-set here. check_cxx_source_compiles() is a
# no-op when its result variable already exists, so a `set(... OFF)` in front
# of it silently skips the test and leaves the feature off forever -- which is
# exactly what happened the first time this was written.
include(CheckCXXSourceCompiles)
set(_chadvis_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
set(_chadvis_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
set(CMAKE_REQUIRED_INCLUDES ${FFMPEG_INCLUDE_DIRS})
# The FULL library paths, not the bare names. A try_compile project does not
# inherit this directory's link_directories(), so "-lavutil" alone resolves
# only against the linker's DEFAULT search path -- which is where Ubuntu's
# /usr/lib/x86_64-linux-gnu is and Homebrew's /opt/homebrew/lib and vcpkg's
# tree are not. That is exactly the shape of the first CI run: the probe
# passed on Ubuntu and failed on macOS and Windows, and the feature silently
# compiled out on two of the three platforms.
set(_chadvis_aes_libraries ${FFMPEG_LINK_LIBRARIES})
if(NOT _chadvis_aes_libraries)
    # The manual-search path has names and a directory, not full paths.
    foreach(_lib IN LISTS FFMPEG_LIBRARIES)
        list(APPEND _chadvis_aes_libraries "${FFMPEG_LIBRARY_DIRS}/${_lib}")
    endforeach()
endif()
set(CMAKE_REQUIRED_LIBRARIES ${_chadvis_aes_libraries})
# The extern "C" wrapper is not optional: libavutil's public headers carry no
# C++ guard of their own, so a C++ translation unit that includes them
# without one declares mangled symbols and fails to link against the C
# library. src/recorder/FFmpegUtils.hpp does the same thing.
# The probe is re-run on every configure rather than cached: a FAILED check is
# cached as an empty entry, so one bad configure (a toolchain that was moved
# mid-flight, say) would switch the feature off permanently with no way back
# short of deleting the cache by hand.
unset(CHADVIS_AVAES_PROBE CACHE)
check_cxx_source_compiles("
extern \"C\" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}
int main() {
    struct AVAES* a = av_aes_alloc();
    if (!a) return 1;
    static const unsigned char k[32] = {0};
    if (av_aes_init(a, k, 256, 0) < 0) return 1;
    unsigned char in[16] = {0};
    unsigned char out[16] = {0};
    av_aes_crypt(a, out, in, 1, nullptr, 0);
    av_free(a);
    return 0;
}" CHADVIS_AVAES_PROBE)
set(CMAKE_REQUIRED_INCLUDES "${_chadvis_saved_required_includes}")
set(CMAKE_REQUIRED_LIBRARIES "${_chadvis_saved_required_libraries}")
unset(_chadvis_saved_required_includes)
unset(_chadvis_saved_required_libraries)
unset(_chadvis_aes_libraries)
unset(_lib)
if(CHADVIS_AVAES_PROBE)
    set(CHADVIS_HAVE_AVAES_INIT ON)
    message(STATUS
        "Encrypted playback: libavutil AES is usable -- the mango channel is enabled")
else()
    set(CHADVIS_HAVE_AVAES_INIT OFF)
    message(STATUS
        "Encrypted playback: libavutil's AES API is not the expected shape -- the "
        "mango channel compiles out and reports itself unavailable at runtime. "
        "This is not a build error; every other feature is unaffected.")
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
        # pkg-config can name a prefix that is not on the default search path;
        # the FFMPEG block above closes the same gap the same way.
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

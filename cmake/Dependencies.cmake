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
#   * With -DCHADVIS_AUTO_INSTALL_DEPS=ON, a missed probe gets one attempt at
#     installing the dependency through the host package manager
#     (cmake/DependencyBootstrap.cmake) before the fallback applies.

include(DependencyBootstrap)

find_package(Qt6 QUIET COMPONENTS
    Core Gui Multimedia Network Quick Qml QuickControls2 Sql DBus)
if(NOT Qt6_FOUND)
    # One attempt at a host package install before failing with a message a
    # human can act on (find_package(REQUIRED) alone would fail too, tersely).
    chadvis_try_install_dependency(QT6)
    if(CHADVIS_INSTALL_OK)
        find_package(Qt6 QUIET COMPONENTS
            Core Gui Multimedia Network Quick Qml QuickControls2 Sql DBus)
    endif()
endif()
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 (6.7+) with components Core Gui Multimedia Network Quick Qml "
        "QuickControls2 Sql DBus was not found. Install it with the Qt online "
        "installer, 'pip install aqtinstall', 'brew install qt' (macOS), or "
        "your distro's Qt6 packages, then re-run CMake. "
        "-DCHADVIS_AUTO_INSTALL_DEPS=ON attempts the install automatically on "
        "Homebrew/MacPorts/Arch/Fedora (Debian/Ubuntu's Qt is older than the "
        "6.7 floor, so it is deliberately not attempted there).")
endif()

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
find_package(OpenGL QUIET)
if(NOT OPENGL_FOUND)
    chadvis_try_install_dependency(OPENGL)
    if(CHADVIS_INSTALL_OK)
        find_package(OpenGL QUIET)
    endif()
endif()
if(NOT OPENGL_FOUND)
    message(FATAL_ERROR
        "OpenGL not found. Install your platform's OpenGL development files "
        "(e.g. 'libgl1-mesa-dev' on Debian/Ubuntu, 'mesa-libGL-devel' on "
        "Fedora, 'mesa' on Arch) or re-run with "
        "-DCHADVIS_AUTO_INSTALL_DEPS=ON to attempt the install automatically.")
endif()

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
if(NOT TARGET fmt::fmt)
    chadvis_try_install_dependency(FMT)
    if(CHADVIS_INSTALL_OK)
        find_package(fmt CONFIG QUIET)
    endif()
endif()
if(TARGET fmt::fmt)
    message(STATUS "Using system fmt")
else()
    CPMAddPackage(
        NAME fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.2
        OPTIONS "FMT_INSTALL OFF"
    )
endif()

# spdlog - logging
find_package(spdlog CONFIG QUIET)
if(NOT TARGET spdlog::spdlog)
    chadvis_try_install_dependency(SPDLOG)
    if(CHADVIS_INSTALL_OK)
        find_package(spdlog CONFIG QUIET)
    endif()
endif()
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

# toml++ - config parsing
find_package(tomlplusplus CONFIG QUIET)
if(NOT TARGET tomlplusplus::tomlplusplus)
    chadvis_try_install_dependency(TOMLPLUSPLUS)
    if(CHADVIS_INSTALL_OK)
        find_package(tomlplusplus CONFIG QUIET)
    endif()
endif()
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

# TagLib - audio metadata.
# The probe is a function so a miss can be retried after a package install
# without re-running the whole file.
function(_chadvis_probe_taglib)
    # find_path/find_library cache their results; clear for a deterministic
    # re-probe after an install.
    unset(_taglib_inc CACHE)
    unset(_taglib_lib CACHE)

    set(_found FALSE)
    set(_via "")
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(TAGLIB taglib)
        if(TAGLIB_FOUND)
            set(_found TRUE)
            set(_via "pkg-config (${TAGLIB_VERSION})")
        endif()
    endif()
    if(NOT _found)
        # Manual fallback, so vcpkg-style layouts work without pkg-config.
        find_path(_taglib_inc taglib/tag.h)
        find_library(_taglib_lib NAMES tag)
        if(_taglib_inc AND _taglib_lib)
            set(TAGLIB_INCLUDE_DIRS "${_taglib_inc}")
            set(TAGLIB_LIBRARIES "${_taglib_lib}")
            set(_found TRUE)
            set(_via "manual search (${_taglib_lib})")
        endif()
    endif()
    set(TAGLIB_FOUND ${_found} PARENT_SCOPE)
    set(TAGLIB_INCLUDE_DIRS "${TAGLIB_INCLUDE_DIRS}" PARENT_SCOPE)
    set(TAGLIB_LIBRARIES "${TAGLIB_LIBRARIES}" PARENT_SCOPE)
    set(TAGLIB_VERSION "${TAGLIB_VERSION}" PARENT_SCOPE)
    set(_CHADVIS_TAGLIB_VIA "${_via}" PARENT_SCOPE)
endfunction()

_chadvis_probe_taglib()
if(NOT TAGLIB_FOUND)
    chadvis_try_install_dependency(TAGLIB)
    if(CHADVIS_INSTALL_OK)
        _chadvis_probe_taglib()
    endif()
endif()
if(TAGLIB_FOUND)
    message(STATUS "TagLib found via ${_CHADVIS_TAGLIB_VIA}")
else()
    message(FATAL_ERROR
        "TagLib not found. Install taglib (with development headers) "
        "or point CMake at a prefix containing it. "
        "-DCHADVIS_AUTO_INSTALL_DEPS=ON attempts the install automatically.")
endif()

# glm - math library (config package on Linux/macOS/Homebrew/vcpkg)
find_package(glm QUIET)
if(NOT glm_FOUND)
    chadvis_try_install_dependency(GLM)
    if(CHADVIS_INSTALL_OK)
        find_package(glm QUIET)
    endif()
endif()
if(NOT glm_FOUND)
    message(FATAL_ERROR
        "glm not found. Install it ('libglm-dev' on Debian/Ubuntu, "
        "'glm-devel' on Fedora, 'glm' on Arch/Homebrew/MacPorts/vcpkg) or "
        "re-run with -DCHADVIS_AUTO_INSTALL_DEPS=ON to attempt the install "
        "automatically.")
endif()

# FFmpeg - libavcodec libavformat libavutil libswscale libswresample.
# The probe is a function so a miss can be retried after a package install.
set(CHADVIS_FFMPEG_COMPONENTS avcodec avformat avutil swscale swresample)
function(_chadvis_probe_ffmpeg)
    foreach(_comp IN LISTS CHADVIS_FFMPEG_COMPONENTS)
        string(TOUPPER "${_comp}" _comp_uc)
        unset(FFMPEG_${_comp_uc}_INCLUDE_DIR CACHE)
        unset(FFMPEG_${_comp_uc}_LIBRARY CACHE)
    endforeach()

    set(_found FALSE)
    set(_via "")
    set(_missing "")
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG
            libavcodec libavformat libavutil libswscale libswresample)
        if(FFMPEG_FOUND)
            set(_found TRUE)
            set(_via "pkg-config (${FFMPEG_VERSION})")
        endif()
    endif()
    if(NOT _found)
        # Per-component manual search, so vcpkg-style layouts work without
        # pkg-config (e.g. Windows).
        set(_inc_dirs "")
        set(_libs "")
        foreach(_comp IN LISTS CHADVIS_FFMPEG_COMPONENTS)
            string(TOUPPER "${_comp}" _comp_uc)
            find_path(FFMPEG_${_comp_uc}_INCLUDE_DIR
                lib${_comp}/${_comp}.h)
            find_library(FFMPEG_${_comp_uc}_LIBRARY
                NAMES ${_comp})
            if(NOT FFMPEG_${_comp_uc}_INCLUDE_DIR OR NOT FFMPEG_${_comp_uc}_LIBRARY)
                list(APPEND _missing ${_comp})
            else()
                list(APPEND _inc_dirs ${FFMPEG_${_comp_uc}_INCLUDE_DIR})
                list(APPEND _libs ${FFMPEG_${_comp_uc}_LIBRARY})
            endif()
        endforeach()
        if(_missing STREQUAL "")
            list(REMOVE_DUPLICATES _inc_dirs)
            set(FFMPEG_INCLUDE_DIRS "${_inc_dirs}")
            set(FFMPEG_LIBRARIES "${_libs}")
            set(_found TRUE)
            set(_via "manual search (${_libs})")
        endif()
    endif()
    set(FFMPEG_FOUND ${_found} PARENT_SCOPE)
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIRS}" PARENT_SCOPE)
    set(FFMPEG_LIBRARIES "${FFMPEG_LIBRARIES}" PARENT_SCOPE)
    set(FFMPEG_VERSION "${FFMPEG_VERSION}" PARENT_SCOPE)
    set(_CHADVIS_FFMPEG_VIA "${_via}" PARENT_SCOPE)
    set(_CHADVIS_FFMPEG_MISSING "${_missing}" PARENT_SCOPE)
endfunction()

_chadvis_probe_ffmpeg()
if(NOT FFMPEG_FOUND)
    chadvis_try_install_dependency(FFMPEG)
    if(CHADVIS_INSTALL_OK)
        _chadvis_probe_ffmpeg()
    endif()
endif()
if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found via ${_CHADVIS_FFMPEG_VIA}")
else()
    if(_CHADVIS_FFMPEG_MISSING)
        set(_missing_msg " Missing components: ${_CHADVIS_FFMPEG_MISSING}.")
    else()
        set(_missing_msg "")
    endif()
    message(FATAL_ERROR
        "FFmpeg not found.${_missing_msg} Install ffmpeg development "
        "packages (libavcodec, libavformat, libavutil, libswscale, "
        "libswresample) or ensure the import libraries are on CMake's "
        "search path (e.g. via vcpkg toolchain file). "
        "-DCHADVIS_AUTO_INSTALL_DEPS=ON attempts the install automatically.")
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

# libavfilter probe as a function so a miss can be retried after a package
# install. Stays non-fatal: a miss only disables burn-in at runtime.
function(_chadvis_probe_avfilter)
    unset(AVFILTER_INCLUDE_DIR CACHE)
    unset(AVFILTER_LIBRARY CACHE)

    set(_found FALSE)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(AVFILTER QUIET libavfilter)
        if(AVFILTER_FOUND)
            set(_found TRUE)
        endif()
    endif()
    if(NOT _found)
        # Non-fatal manual fallback, mirroring the FFmpeg loop above.
        find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h)
        find_library(AVFILTER_LIBRARY NAMES avfilter)
        if(AVFILTER_INCLUDE_DIR AND AVFILTER_LIBRARY)
            set(AVFILTER_INCLUDE_DIRS "${AVFILTER_INCLUDE_DIR}")
            set(AVFILTER_LIBRARIES "${AVFILTER_LIBRARY}")
            set(_found TRUE)
        endif()
    endif()
    set(AVFILTER_FOUND ${_found} PARENT_SCOPE)
    set(AVFILTER_INCLUDE_DIRS "${AVFILTER_INCLUDE_DIRS}" PARENT_SCOPE)
    set(AVFILTER_LIBRARIES "${AVFILTER_LIBRARIES}" PARENT_SCOPE)
    set(AVFILTER_LIBRARY_DIRS "${AVFILTER_LIBRARY_DIRS}" PARENT_SCOPE)
    set(AVFILTER_VERSION "${AVFILTER_VERSION}" PARENT_SCOPE)
endfunction()

set(CHADVIS_HAS_AVFILTER OFF)
if(CHADVIS_POSTPROCESS)
    _chadvis_probe_avfilter()
    if(NOT AVFILTER_FOUND)
        chadvis_try_install_dependency(AVFILTER)
        if(CHADVIS_INSTALL_OK)
            _chadvis_probe_avfilter()
        endif()
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
        if(AVFILTER_VERSION)
            set(_avfilter_desc "libavfilter ${AVFILTER_VERSION}")
        else()
            set(_avfilter_desc "libavfilter (${AVFILTER_LIBRARIES})")
        endif()
        message(STATUS
            "Post-pass: ${_avfilter_desc} found -- burn-in enabled")
    else()
        message(STATUS
            "Post-pass: libavfilter NOT found. CHADVIS_POSTPROCESS is ON but "
            "burn-in will report itself unsupported at runtime. This is not a "
            "build error; every other feature is unaffected. Install "
            "libavfilter, or pass -DCHADVIS_POSTPROCESS=OFF to skip the probe.")
    endif()
else()
    message(STATUS
        "Post-pass: disabled by CHADVIS_POSTPROCESS=OFF -- burn-in compiled out")
endif()


# ---------------------------------------------------------------------------
# projectM v4 (system detection with CPM source-build fallback)
# ---------------------------------------------------------------------------

include(FindProjectM4)

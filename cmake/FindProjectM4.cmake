# Version: 2.1.0 - 2026-10-18
# FindProjectM4.cmake - locate or build projectM v4 (core + playlist libraries).
#
# Detection order:
#   1. CMake package config   - installed by libprojectm as projectM4Config.cmake
#   2. pkg-config             - Linux only; raw linker flags are inherently
#                               platform-specific so we don't pretend they port
#   2b. Host package manager  - one install attempt when
#                               -DCHADVIS_AUTO_INSTALL_DEPS=ON, then 1-2 re-run
#   3. CPM source build       - always available last resort (static libs)
#
# Results for consumers:
#   PROJECTM_FOUND          TRUE when a usable projectM v4 is available
#   PROJECTM_LINK_TARGETS   imported/ALIAS CMake targets to link (preferred;
#                           headers propagate transitively through them)
#   PROJECTM_LINK_FLAGS     raw linker flags (only set in the pkg-config path)
#   PROJECTM_INCLUDE_DIRS   extra include dirs (empty for target-based paths)

option(CHADVIS_FORCE_CPM_PROJECTM
    "Force building libprojectm from source via CPM" OFF)

# System probes as a function so a miss can be retried after a package
# install (cmake/DependencyBootstrap.cmake) without re-running this file.
function(_projectm_probe_system)
    set(PROJECTM_FOUND FALSE PARENT_SCOPE)
    set(PROJECTM_LINK_TARGETS "" PARENT_SCOPE)
    set(PROJECTM_LINK_FLAGS "" PARENT_SCOPE)
    set(PROJECTM_INCLUDE_DIRS "" PARENT_SCOPE)
    set(_CHADVIS_PROJECTM_VIA "" PARENT_SCOPE)

    if(CHADVIS_FORCE_CPM_PROJECTM)
        return()
    endif()

    # ── 1. CMake config package ─────────────────────────────────────────
    # Upstream v4 exports namespace `libprojectM::` with targets
    # `projectM` and `playlist` (i.e. libprojectM::projectM, libprojectM::playlist).
    # Imported targets land in directory scope even when created here.
    find_package(projectM4 CONFIG QUIET)
    if(projectM4_FOUND AND TARGET libprojectM::projectM AND TARGET libprojectM::playlist)
        set(PROJECTM_LINK_TARGETS libprojectM::projectM libprojectM::playlist PARENT_SCOPE)
        set(PROJECTM_FOUND TRUE PARENT_SCOPE)
        set(_CHADVIS_PROJECTM_VIA "CMake package config" PARENT_SCOPE)
        return()
    endif()

    # ── 2. pkg-config fallback (Linux) ──────────────────────────────────
    if(PKG_CONFIG_FOUND AND UNIX AND NOT APPLE)
        pkg_check_modules(PROJECTM_PC projectM-4 QUIET)
        if(PROJECTM_PC_FOUND)
            # Some projectM .pc files emit GNU-ld-only `-l:name` syntax;
            # normalize to plain `-lname`, understood by every Unix linker.
            string(REGEX REPLACE "-l:([^ ;]+)" "-l\\1" _flags
                "${PROJECTM_PC_LDFLAGS}")
            set(PROJECTM_LINK_FLAGS "${_flags}" PARENT_SCOPE)
            set(PROJECTM_INCLUDE_DIRS "${PROJECTM_PC_INCLUDE_DIRS}" PARENT_SCOPE)
            set(PROJECTM_FOUND TRUE PARENT_SCOPE)
            set(_CHADVIS_PROJECTM_VIA "pkg-config" PARENT_SCOPE)
        endif()
    endif()
endfunction()

_projectm_probe_system()
if(NOT PROJECTM_FOUND)
    chadvis_try_install_dependency(PROJECTM)
    if(CHADVIS_INSTALL_OK)
        _projectm_probe_system()
    endif()
endif()
if(PROJECTM_FOUND)
    message(STATUS "Found projectM v4 via ${_CHADVIS_PROJECTM_VIA}")
endif()

# ── 3. Build from source via CPM ────────────────────────────────────────
if(NOT PROJECTM_FOUND)
    if(CHADVIS_FORCE_CPM_PROJECTM)
        message(STATUS "Building libprojectm from source via CPM (forced)...")
    else()
        message(STATUS "projectM v4 not found on system. Building from source via CPM...")
    endif()

    # Valid options for upstream v4.x: BUILD_SHARED_LIBS, ENABLE_PLAYLIST,
    # ENABLE_SDL_UI, BUILD_TESTING, BUILD_DOCS, ENABLE_* feature toggles.
    # Static libs avoid runtime dylib/dll resolution entirely.
    CPMAddPackage(
        NAME projectm
        GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
        # v4.1.6, pinned to the commit the tag names. The upgrade to 4.1.8 is
        # T0164 in TODO.md, not a version to float.
        GIT_TAG 3158ee615eaafd93a8912b5f6dd84a9c47b2e00a
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "ENABLE_PLAYLIST ON"
            "ENABLE_SDL_UI OFF"
            "BUILD_TESTING OFF"
    )

    if(TARGET libprojectM::projectM AND TARGET libprojectM::playlist)
        set(PROJECTM_LINK_TARGETS libprojectM::projectM libprojectM::playlist)
        set(PROJECTM_FOUND TRUE)
        # Headers (<projectM-4/projectM.h>, <projectM-4/playlist.h>) arrive
        # transitively: projectM_playlist -> projectM -> projectM_api all use
        # PUBLIC $<BUILD_INTERFACE> include dirs in the upstream build tree.
    else()
        message(FATAL_ERROR
            "CPM projectm source build did not produce the expected targets "
            "(libprojectM::projectM, libprojectM::playlist). Upstream layout "
            "may have changed.")
    endif()
endif()

if(NOT PROJECTM_FOUND)
    message(FATAL_ERROR
        "projectM v4 could not be found or built. Install libprojectm "
        "(with development headers) or allow the CPM source build.")
endif()

# Version: 2.0.0 - 2026-08-25
# FindProjectM4.cmake - locate or build projectM v4 (core + playlist libraries).
#
# Detection order:
#   1. CMake package config   - installed by libprojectm as projectM4Config.cmake
#   2. pkg-config             - Linux only; raw linker flags are inherently
#                               platform-specific so we don't pretend they port
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

set(PROJECTM_FOUND FALSE)

if(NOT CHADVIS_FORCE_CPM_PROJECTM)
    # ── 1. CMake config package ─────────────────────────────────────────
    # Upstream v4 exports namespace `libprojectM::` with targets
    # `projectM` and `playlist` (i.e. libprojectM::projectM, libprojectM::playlist).
    find_package(projectM4 CONFIG QUIET)
    if(projectM4_FOUND AND TARGET libprojectM::projectM AND TARGET libprojectM::playlist)
        message(STATUS "Found projectM v4 via CMake package config")
        set(PROJECTM_LINK_TARGETS libprojectM::projectM libprojectM::playlist)
        set(PROJECTM_FOUND TRUE)
    endif()

    # ── 2. pkg-config fallback (Linux) ──────────────────────────────────
    if(NOT PROJECTM_FOUND AND PKG_CONFIG_FOUND AND UNIX AND NOT APPLE)
        pkg_check_modules(PROJECTM_PC projectM-4 QUIET)
        if(PROJECTM_PC_FOUND)
            # Some projectM .pc files emit GNU-ld-only `-l:name` syntax;
            # normalize to plain `-lname`, understood by every Unix linker.
            string(REGEX REPLACE "-l:([^ ;]+)" "-l\\1" PROJECTM_LINK_FLAGS
                "${PROJECTM_PC_LDFLAGS}")
            set(PROJECTM_INCLUDE_DIRS "${PROJECTM_PC_INCLUDE_DIRS}")
            set(PROJECTM_FOUND TRUE)
            message(STATUS "Found projectM v4 via pkg-config")
        endif()
    endif()
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
        GIT_TAG v4.1.6
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

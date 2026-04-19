# Version: 1.0.0 - 2026-04-19 14:00:00 MDT
# FindProjectM4.cmake - Intelligent detection for ProjectM v4 with CPM fallback

# Option to force CPM build (useful when system version is outdated)
option(CHADVIS_FORCE_CPM_PROJECTM "Force building libprojectm from source via CPM" OFF)

if(CHADVIS_FORCE_CPM_PROJECTM)
    message(STATUS "CHADVIS_FORCE_CPM_PROJECTM is set, skipping system libprojectm detection")
else()
    # 1. Try modern CMake config (installed by libprojectm on Arch)
    find_package(projectM4 CONFIG QUIET)
    if(projectM4_FOUND)
        message(STATUS "Found ProjectM v4 via CMake config")
        set(PROJECTM_LIBRARIES libprojectM::projectM libprojectM::playlist)
        set(PROJECTM_FOUND TRUE)
    endif()

    # 2. Try pkg-config
    if(NOT PROJECTM_FOUND)
        pkg_check_modules(PROJECTM projectM-4 QUIET)
        if(NOT PROJECTM_FOUND)
            pkg_check_modules(PROJECTM libprojectM QUIET)
        endif()
    endif()

    # 3. Try manual search (fallback for custom installs)
    if(NOT PROJECTM_FOUND)
        find_path(PROJECTM_INCLUDE_DIRS projectM-4/projectM.h
            HINTS /usr/local/include /usr/include
        )
        find_library(PROJECTM_LIBRARIES projectM-4
            HINTS /usr/local/lib /usr/lib
        )
        if(PROJECTM_INCLUDE_DIRS AND PROJECTM_LIBRARIES)
            set(PROJECTM_FOUND TRUE)
            message(STATUS "Found ProjectM v4 manually: ${PROJECTM_LIBRARIES}")
        endif()
    endif()
endif()

# 4. CPM Fallback - Build from source
if(NOT PROJECTM_FOUND)
    if(CHADVIS_FORCE_CPM_PROJECTM)
        message(STATUS "Building libprojectm from source via CPM (forced)...")
    else()
        message(STATUS "ProjectM v4 not found on system. Building from source via CPM...")
    endif()
    CPMAddPackage(
        NAME projectm
        GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
        GIT_TAG v4.1.6
        OPTIONS 
            "BUILD_PROJECTM_STATIC ON"
            "BUILD_PROJECTM_SAMPLES OFF"
            "BUILD_PROJECTM_TESTS OFF"
            "BUILD_PROJECTM_JACK OFF"
            "BUILD_PROJECTM_PULSE OFF"
            "BUILD_PROJECTM_SDL OFF"
            "BUILD_PROJECTM_QT OFF"
    )
    if(projectm_FOUND)
        set(PROJECTM_LIBRARIES projectM projectM-playlist)
        set(PROJECTM_FOUND TRUE)
        # Ensure headers are found with projectM-4 prefix if built as subproject
        # ProjectM v4 headers are usually in <src>/src/libprojectM
        target_include_directories(projectM INTERFACE $<BUILD_INTERFACE:${projectm_SOURCE_DIR}/src/libprojectM>)
    endif()
endif()

if(NOT PROJECTM_FOUND)
    message(FATAL_ERROR "ProjectM v4 could not be found or built. Please install libprojectm (Arch) or build from source.")
endif()

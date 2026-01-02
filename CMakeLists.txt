cmake_minimum_required(VERSION 3.20)
# Remove problematic compiler flags that cause build failures
# These flags can come from system config or Qt defaults
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# Also set them explicitly to ensure they stay clean
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "" FORCE)
project(chadvis-projectm-qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_AUTOMOC ON)
# Near the top, after project()
set(CMAKE_AUTOMOC ON)
# fsks compile up set(CMAKE_AUTOMOC_MOC_OPTIONS "-b" "stdafx.h")
# Or, more simply, ensure headers are listed in sources
# The key is that headers with Q_OBJECT must be in the source list

set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings (we're professionals here)
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Config-specific options
add_compile_options(
    "$<$<CONFIG:Debug>:-g3;-O0>"
    "$<$<CONFIG:Release>:-O3;-march=native>"
)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia OpenGLWidgets Svg Network)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SPDLOG REQUIRED spdlog)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(TAGLIB REQUIRED taglib)
pkg_check_modules(TOMLPP REQUIRED tomlplusplus)
pkg_check_modules(GLEW REQUIRED glew)
find_package(glm REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)

# ProjectM - try pkg-config first, fallback to manual
# Local ProjectM v4 installation
set(PROJECTM_LOCAL_DIR "${CMAKE_SOURCE_DIR}/external/projectm-install")
if(EXISTS "${PROJECTM_LOCAL_DIR}/include/projectM-4/projectM.h")
    message(STATUS "Using local ProjectM v4 from ${PROJECTM_LOCAL_DIR}")
    set(PROJECTM_INCLUDE_DIRS "${PROJECTM_LOCAL_DIR}/include")
    set(PROJECTM_LIBRARIES "${PROJECTM_LOCAL_DIR}/lib/libprojectM-4.so")
    set(PROJECTM_FOUND TRUE)
    set(CMAKE_INSTALL_RPATH "${PROJECTM_LOCAL_DIR}/lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

pkg_check_modules(PROJECTM projectM-4)
if(NOT PROJECTM_FOUND)
    # Fallback: try libprojectM
    pkg_check_modules(PROJECTM libprojectM)
endif()
if(NOT PROJECTM_FOUND)
    # Try to find manually built projectM
    find_path(PROJECTM_INCLUDE_DIRS projectM-4/projectM.h
        HINTS /usr/local/include /usr/include
    )
    find_library(PROJECTM_LIBRARIES projectM-4
        HINTS /usr/local/lib /usr/lib
    )
    if(PROJECTM_INCLUDE_DIRS AND PROJECTM_LIBRARIES)
        set(PROJECTM_FOUND TRUE)
        message(STATUS "Found ProjectM manually: ${PROJECTM_LIBRARIES}")
    else()
        message(FATAL_ERROR "ProjectM not found. Install from AUR or build from source.")
    endif()
endif()

# Fix pkg-config bug: -l:projectM-4 -> -lprojectM-4
if(PROJECTM_FOUND AND PROJECTM_LDFLAGS)
    string(REPLACE "-l:projectM-4" "-lprojectM-4" PROJECTM_LDFLAGS_FIXED "${PROJECTM_LDFLAGS}")
    set(PROJECTM_LDFLAGS "${PROJECTM_LDFLAGS_FIXED}")
endif()

# Source files organized by module - HEADERS MUST BE LISTED for AUTOMOC
set(UTIL_SOURCES
    src/util/Types.hpp
    src/util/Result.hpp
    src/util/Signal.hpp
    src/util/FileUtils.hpp
    src/util/FileUtils.cpp
)

set(CORE_SOURCES
    src/core/Logger.hpp
    src/core/Logger.cpp
    src/core/Config.hpp
    src/core/Config.cpp
    src/core/Application.hpp
    src/core/Application.cpp
)

set(AUDIO_SOURCES
    src/audio/AudioEngine.hpp
    src/audio/AudioEngine.cpp
    src/audio/AudioAnalyzer.hpp
    src/audio/AudioAnalyzer.cpp
    src/audio/Playlist.hpp
    src/audio/Playlist.cpp
    src/audio/MediaMetadata.hpp
    src/audio/MediaMetadata.cpp
    src/audio/FFmpegAudioSource.hpp
    src/audio/FFmpegAudioSource.cpp
)

set(VISUALIZER_SOURCES
    src/visualizer/ProjectMBridge.hpp
    src/visualizer/ProjectMBridge.cpp
    src/visualizer/PresetManager.hpp
    src/visualizer/PresetManager.cpp
    src/visualizer/RatingManager.hpp
    src/visualizer/RatingManager.cpp
    src/visualizer/RenderTarget.hpp
    src/visualizer/RenderTarget.cpp
    src/visualizer/VisualizerWindow.hpp
    src/visualizer/VisualizerWindow.cpp
)

set(SUNO_SOURCES
    src/suno/SunoModels.hpp
    src/suno/SunoClient.hpp
    src/suno/SunoClient.cpp
)

set(OVERLAY_SOURCES
    src/overlay/TextElement.hpp
    src/overlay/TextElement.cpp
    src/overlay/TextAnimator.hpp
    src/overlay/TextAnimator.cpp
    src/overlay/OverlayConfig.hpp
    src/overlay/OverlayConfig.cpp
    src/overlay/OverlayEngine.hpp
    src/overlay/OverlayEngine.cpp
    src/overlay/OverlayRenderer.hpp
    src/overlay/OverlayRenderer.cpp
)

set(RECORDER_SOURCES
    src/recorder/EncoderSettings.hpp
    src/recorder/EncoderSettings.cpp
    src/recorder/FrameGrabber.hpp
    src/recorder/FrameGrabber.cpp
    src/recorder/VideoRecorder.hpp
    src/recorder/VideoRecorder.cpp
)

set(UI_SOURCES
    src/ui/controllers/AudioController.hpp
    src/ui/controllers/AudioController.cpp
    src/ui/controllers/VisualizerController.hpp
    src/ui/controllers/VisualizerController.cpp
    src/ui/controllers/RecordingController.hpp
    src/ui/controllers/RecordingController.cpp
    src/ui/PlayerControls.hpp
    src/ui/PlayerControls.cpp
    src/ui/PlaylistView.hpp
    src/ui/PlaylistView.cpp
    src/ui/VisualizerPanel.hpp
    src/ui/VisualizerPanel.cpp
    src/ui/OverlayEditor.hpp
    src/ui/OverlayEditor.cpp
    src/ui/PresetBrowser.hpp
    src/ui/PresetBrowser.cpp
    src/ui/RecordingControls.hpp
    src/ui/RecordingControls.cpp
    src/ui/SettingsDialog.hpp
    src/ui/SettingsDialog.cpp
    src/ui/MainWindow.hpp
    src/ui/MainWindow.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${UTIL_SOURCES}
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${VISUALIZER_SOURCES}
    ${SUNO_SOURCES}
    ${OVERLAY_SOURCES}
    ${RECORDER_SOURCES}
    ${UI_SOURCES}
)

# Qt resources
set(RESOURCES resources/chadvis-projectm-qt.qrc)

# Main executable
add_executable(chadvis-projectm-qt ${ALL_SOURCES} ${RESOURCES})

target_include_directories(chadvis-projectm-qt PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${TOMLPP_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${PROJECTM_INCLUDE_DIRS}
)

target_link_libraries(chadvis-projectm-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Network
    ${SPDLOG_LIBRARIES}
    ${FMT_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    projectM-4
    projectM-4-playlist
    OpenGL
)

if(PULSEAUDIO_FOUND)
    target_link_libraries(chadvis-projectm-qt PRIVATE ${PULSEAUDIO_LIBRARIES} pulse-simple)
    target_include_directories(chadvis-projectm-qt PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})
endif()

# Installation
install(TARGETS chadvis-projectm-qt DESTINATION bin)
install(DIRECTORY config/ DESTINATION share/chadvis-projectm-qt/config)
install(FILES resources/chadvis-projectm-qt.desktop DESTINATION share/applications)
install(FILES resources/icons/chadvis-projectm-qt.svg DESTINATION share/icons/hicolor/scalable/apps)

# CPack for packaging
set(CPACK_PACKAGE_NAME "chadvis-projectm-qt")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt6 projectM v4 visualizer with modern C++20")
include(CPack)

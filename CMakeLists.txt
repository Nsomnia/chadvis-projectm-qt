cmake_minimum_required(VERSION 3.20)
# Remove problematic compiler flags that cause build failures
# These flags can come from system config or Qt defaults
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

### cmake/ninja machine readable token efficiency
## To make CMake output more token-efficient for an agent, you can modify the verbosity settings. Here are a few approaches:
# 1. Use --no-color flag:
# cmake --no-color [options] [source_dir]
# 2. Set CMAKE_COLOR_NONE environment variable:
# export CMAKE_COLOR_NONE=ON
# 3. Modify CMakeLists.txt:
set(CMAKE_COLOR_NONE ON)
# 4. Use --output-on-error instead of --verbose:
# cmake --output-on-error [options] [source_dir]
# 5. Redirect output to /dev/null:
# cmake [options] [source_dir] > /dev/null 2>&1

# Also set them explicitly to ensure they stay clean
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "" FORCE)
project(chadvis-projectm-qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Disable global Qt automatic features to prevent overhead on dependencies (like kissfft)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include CPM.cmake for header-only libraries
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/CPM.cmake")
    file(DOWNLOAD
         https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.0/CPM.cmake
         ${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia OpenGLWidgets Svg Network Sql WebEngineWidgets Quick Qml QuickControls2)
find_package(PkgConfig REQUIRED)

# Header-only libraries: try system first, fallback to CPM
# spdlog - for logging
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

# fmt - for string formatting
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

# toml++ - for config parsing
find_package(tomlplusplus CONFIG QUIET)
if(TARGET tomlplusplus::tomlplusplus)
    message(STATUS "Using system toml++")
else()
    CPMAddPackage(
        NAME tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        VERSION 3.4.0
    )
endif()

# KissFFT - replace custom FFT implementation
CPMAddPackage(
    NAME kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG 131.1.0
    VERSION 131.1.0
    OPTIONS "KISS_FFT_STATIC ON" "KISS_FFT_DATATYPE=float" "KISS_FFT_USE_ALLOCA ON"
)

# Large libraries - keep as system dependencies
pkg_check_modules(TAGLIB REQUIRED taglib)
pkg_check_modules(GLEW REQUIRED glew)
find_package(glm REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)

# ProjectM - Intelligent detection with CPM fallback
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

# Fix pkg-config bug: -l:projectM-4 -> -lprojectM-4
if(PROJECTM_LDFLAGS)
    string(REPLACE "-l:projectM-4" "-lprojectM-4" PROJECTM_LDFLAGS_FIXED "${PROJECTM_LDFLAGS}")
    set(PROJECTM_LDFLAGS "${PROJECTM_LDFLAGS_FIXED}")
endif()

# Source files organized by module
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
    src/core/ConfigData.hpp
    src/core/ConfigParsers.hpp
    src/core/ConfigParsers.cpp
    src/core/ConfigLoader.hpp
    src/core/ConfigLoader.cpp
    src/core/Config.hpp
    src/core/Config.cpp
    src/core/CliUtils.hpp
    src/core/CliUtils.cpp
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
    src/visualizer/projectm/Config.hpp
    src/visualizer/projectm/Engine.hpp
    src/visualizer/projectm/Engine.cpp
    src/visualizer/projectm/Playlist.hpp
    src/visualizer/projectm/Playlist.cpp
    src/visualizer/projectm/Bridge.hpp
    src/visualizer/projectm/Bridge.cpp
    src/visualizer/PresetData.hpp
    src/visualizer/PresetScanner.hpp
    src/visualizer/PresetScanner.cpp
    src/visualizer/PresetPersistence.hpp
    src/visualizer/PresetPersistence.cpp
    src/visualizer/PresetManager.hpp
    src/visualizer/PresetManager.cpp
    src/visualizer/RatingManager.hpp
    src/visualizer/RatingManager.cpp
    src/visualizer/RenderTarget.hpp
    src/visualizer/RenderTarget.cpp
    src/visualizer/VisualizerRenderer.hpp
    src/visualizer/VisualizerRenderer.cpp
    src/visualizer/VisualizerWindow.hpp
    src/visualizer/VisualizerWindow.cpp
)

set(SUNO_SOURCES
    src/suno/SunoModels.hpp
    src/suno/SunoClient.hpp
    src/suno/SunoClient.cpp
    src/suno/SunoDatabase.hpp
    src/suno/SunoDatabase.cpp
    src/suno/LyricAligner.hpp
    src/suno/SunoLyrics.hpp
    src/suno/SunoLyrics.cpp
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
    src/recorder/VideoRecorderCore.hpp
    src/recorder/VideoRecorderCore.cpp
    src/recorder/VideoRecorderFFmpeg.hpp
    src/recorder/VideoRecorderFFmpeg.cpp
    src/recorder/VideoRecorderThread.hpp
    src/recorder/VideoRecorderThread.cpp
)

set(LYRICS_SOURCES
    src/lyrics/LyricsData.hpp
    src/lyrics/LyricsData.cpp
    src/lyrics/LyricsSync.hpp
    src/lyrics/LyricsSync.cpp
    src/lyrics/LyricsRenderer.hpp
    src/lyrics/LyricsRenderer.cpp
)

# ═══════════════════════════════════════════════════════════════════════════
# QML BRIDGE SOURCES - C++ types exposed to QML
# ═══════════════════════════════════════════════════════════════════════════
set(QML_BRIDGE_SOURCES
    src/qml_bridge/AudioBridge.hpp
    src/qml_bridge/AudioBridge.cpp
    src/qml_bridge/PlaylistBridge.hpp
    src/qml_bridge/PlaylistBridge.cpp
    src/qml_bridge/VisualizerBridge.hpp
    src/qml_bridge/VisualizerBridge.cpp
    src/qml_bridge/RecordingBridge.hpp
    src/qml_bridge/RecordingBridge.cpp
    src/qml_bridge/BridgeRegistration.hpp
    src/qml_bridge/BridgeRegistration.cpp
)

set(UI_SOURCES
    src/ui/controllers/AudioController.hpp
    src/ui/controllers/AudioController.cpp
    src/ui/controllers/VisualizerController.hpp
    src/ui/controllers/VisualizerController.cpp
    src/ui/controllers/RecordingController.hpp
    src/ui/controllers/RecordingController.cpp
    src/ui/controllers/SunoController.hpp
    src/ui/controllers/SunoController.cpp
    src/ui/MarqueeLabel.hpp
    src/ui/MarqueeLabel.cpp
    src/ui/PlayerControls.hpp
    src/ui/PlayerControls.cpp
    src/ui/PlaylistView.hpp
    src/ui/PlaylistView.cpp
    src/ui/VisualizerPanel.hpp
    src/ui/VisualizerPanel.cpp
    src/ui/OverlayEditor.hpp
    src/ui/OverlayEditor.cpp
    src/ui/SunoBrowser.hpp
    src/ui/SunoBrowser.cpp
    src/ui/SunoCookieDialog.hpp
    src/ui/SunoCookieDialog.cpp
    src/ui/KaraokeWidget.hpp
    src/ui/KaraokeWidget.cpp
    src/ui/PresetBrowser.hpp
    src/ui/PresetBrowser.cpp
    src/ui/RecordingControls.hpp
    src/ui/RecordingControls.cpp
    src/ui/SettingsDialog.hpp
    src/ui/SettingsDialog.cpp
    src/ui/MainWindow.hpp
    src/ui/MainWindow.cpp
    src/ui/SunoBrowserWidget.hpp
    src/ui/SunoBrowserWidget.cpp
    src/ui/SunoPersistentAuth.hpp
    src/ui/SunoPersistentAuth.cpp
    src/ui/SystemBrowserAuth.hpp
    src/ui/SystemBrowserAuth.cpp
    src/ui/LyricsPanel.hpp
    src/ui/LyricsPanel.cpp
    src/ui/SidebarWidget.hpp
    src/ui/SidebarWidget.cpp
    src/ui/widgets/CyanSlider.hpp
    src/ui/widgets/CyanSlider.cpp
    src/ui/widgets/GlowButton.hpp
    src/ui/widgets/GlowButton.cpp
    src/ui/widgets/ToggleSwitch.hpp
    src/ui/widgets/ToggleSwitch.cpp
)

# ═══════════════════════════════════════════════════════════════════════════
# QML FILES - Declarative UI components
# ═══════════════════════════════════════════════════════════════════════════
set(QML_SOURCES
    src/qml/main.qml
    src/qml/styles/Theme.qml
    src/qml/components/AccordionPanel.qml
    src/qml/components/AccordionContainer.qml
    src/qml/components/AppButton.qml
    src/qml/components/AppSlider.qml
    src/qml/panels/PlaybackPanel.qml
    src/qml/panels/PlaylistPanel.qml
)

# Common include directories
set(COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${TOMLPP_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${PROJECTM_INCLUDE_DIRS}
    ${KISSFFT_INCLUDE_DIRS}
)

# Common link libraries
set(COMMON_LIBS
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Network
    Qt6::Sql
    Qt6::WebEngineWidgets
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    ${TAGLIB_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    projectM-4
    projectM-4-playlist
    OpenGL
)

# CPM libraries targets
set(CPM_LIBS "")
if(TARGET spdlog::spdlog)
    list(APPEND CPM_LIBS spdlog::spdlog)
elseif(CPM_spdlog)
    list(APPEND CPM_LIBS CPM_spdlog)
endif()

if(TARGET fmt::fmt)
    list(APPEND CPM_LIBS fmt::fmt)
elseif(CPM_fmt)
    list(APPEND CPM_LIBS CPM_fmt)
endif()

if(TARGET tomlplusplus::tomlplusplus)
    list(APPEND CPM_LIBS tomlplusplus::tomlplusplus)
elseif(CPM_tomlplusplus)
    list(APPEND CPM_LIBS CPM_tomlplusplus)
endif()

if(TARGET kissfft::kissfft)
    list(APPEND CPM_LIBS kissfft::kissfft)
elseif(CPM_kissfft)
    list(APPEND CPM_LIBS CPM_kissfft)
endif()

# Define libraries
add_library(project_lib STATIC
    ${UTIL_SOURCES}
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${VISUALIZER_SOURCES}
    ${SUNO_SOURCES}
    ${OVERLAY_SOURCES}
    ${RECORDER_SOURCES}
    ${LYRICS_SOURCES}
    ${UI_SOURCES}
    ${QML_BRIDGE_SOURCES}
    resources/chadvis-projectm-qt.qrc
)

set_target_properties(project_lib PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

target_include_directories(project_lib PUBLIC ${COMMON_INCLUDES})
target_link_libraries(project_lib PUBLIC ${COMMON_LIBS} ${CPM_LIBS})

# ═══════════════════════════════════════════════════════════════════════════
# QML MODULE - Modern UI components
# ═══════════════════════════════════════════════════════════════════════════
qt_policy(SET QTP0001 NEW)

qt_add_qml_module(project_lib
    URI ChadVis
    VERSION 1.0
    QML_FILES ${QML_SOURCES}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/ChadVis
)

# Main executable
add_executable(chadvis-projectm-qt src/main.cpp)
target_link_libraries(chadvis-projectm-qt PRIVATE project_lib)

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

# Tests
add_subdirectory(tests)

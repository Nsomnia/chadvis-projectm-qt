cmake_minimum_required(VERSION 3.20)
project(projectm-qt-visualizer
VERSION 0.1.0
DESCRIPTION "Chad's projectM Visualizer - I use Arch, BTW"
LANGUAGES CXX C
)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6 setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
Core
Gui
Widgets
OpenGL
Multimedia
)

# projectM v4 via pkg-config
find_package(PkgConfig REQUIRED)
set(ENV{PKG_CONFIG_PATH} "/usr/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(PROJECTM4 REQUIRED projectM-4)

# PulseAudio/PipeWire for audio capture
pkg_check_modules(PULSEAUDIO libpulse)
if(PULSEAUDIO_FOUND)
    message(STATUS "PulseAudio/PipeWire found - audio capture enabled")
    add_definitions(-DHAVE_PULSEAUDIO)
else()
    message(WARNING "PulseAudio not found - audio capture disabled")
endif()

# OpenGL
find_package(OpenGL REQUIRED)

# Include directories
include_directories(
${CMAKE_SOURCE_DIR}/src
${PROJECTM4_INCLUDE_DIRS}
)
link_directories(
${PROJECTM4_LIBRARY_DIRS}
)

# Source files
set(SOURCES
src/main_new.cpp
src/ProjectMWindow.cpp
src/projectm/ProjectMWrapper.cpp
src/platform/linux/PulseAudioSource.cpp
src/core/utils/Logger.cpp
)

# Headers
set(HEADERS
src/ProjectMWindow.hpp
src/projectm/ProjectMWrapper.hpp
src/platform/linux/PulseAudioSource.hpp
src/core/utils/Logger.hpp
)

# Main executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
Qt6::Core
Qt6::Gui
Qt6::Widgets
Qt6::OpenGL
Qt6::Multimedia
projectM-4
projectM-4-playlist
OpenGL::GL
)

if(PULSEAUDIO_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PULSEAUDIO_LIBRARIES} pulse-simple)
    target_include_directories(${PROJECT_NAME} PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})
endif()

# Print config
message(STATUS "===========================================")
message(STATUS " projectm-qt-visualizer Configuration")
message(STATUS " I use Arch, BTW")
message(STATUS "===========================================")
message(STATUS " Version: ${PROJECT_VERSION}")
message(STATUS " Qt6: ${Qt6_VERSION}")
message(STATUS " projectM: ${PROJECTM4_VERSION}")
if(PULSEAUDIO_FOUND)
message(STATUS " PulseAudio: YES")
else()
message(STATUS " PulseAudio: NO")
endif()
message(STATUS " Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")

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
OpenGLWidgets
Multimedia
)
# projectM v4 via pkg-config
find_package(PkgConfig REQUIRED)
set(ENV{PKG_CONFIG_PATH} "/usr/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(PROJECTM4 REQUIRED projectM-4)
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
# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
# Print config summary - Chad developers like to know what's up
message(STATUS "===========================================")
message(STATUS " projectm-qt-visualizer Configuration")
message(STATUS " I use Arch, BTW")
message(STATUS "===========================================")
message(STATUS " Version: ${PROJECT_VERSION}")
message(STATUS " Qt6: ${Qt6_VERSION}")
message(STATUS " projectM: ${PROJECTM4_VERSION}")
message(STATUS " Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")

# Compiler.cmake - C++23 requirement, toolchain floor, warnings and optimization flags.
#
# The codebase relies on C++23 facilities (std::jthread, std::print,
# std::expected, std::ranges, ...). Older toolchains fail deep inside
# standard headers with confusing errors, so we reject them up front.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Toolchain floor
# ---------------------------------------------------------------------------

set(_cv_toolchain_ok TRUE)
set(_cv_toolchain_req "")

if(MSVC)
    if(MSVC_VERSION LESS 1929)
        set(_cv_toolchain_ok FALSE)
        set(_cv_toolchain_req "MSVC 19.29 (Visual Studio 2019 16.10) or newer")
    endif()
elseif(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
        set(_cv_toolchain_ok FALSE)
        set(_cv_toolchain_req "AppleClang 15 (Xcode 15) or newer")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        set(_cv_toolchain_ok FALSE)
        set(_cv_toolchain_req "GCC 13 or newer")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        set(_cv_toolchain_ok FALSE)
        set(_cv_toolchain_req "Clang 16 or newer (for jthread/format support)")
    endif()
else()
    message(WARNING
        "Unknown compiler '${CMAKE_CXX_COMPILER_ID}' "
        "${CMAKE_CXX_COMPILER_VERSION}; C++23 support is unverified. "
        "Proceeding, but expect failures on toolchains older than "
        "GCC 13 / Clang 16 / MSVC 19.29.")
endif()

if(NOT _cv_toolchain_ok)
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is too old.\n"
        "ChadVis requires ${_cv_toolchain_req} for full C++23 support "
        "(std::jthread, std::print, std::expected).")
endif()

message(STATUS "Toolchain: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ---------------------------------------------------------------------------
# Warning and optimization flags
# ---------------------------------------------------------------------------

# Optimize Release builds for the host CPU. OFF by default so redistributable
# Release binaries remain portable across machines.
option(CHADVIS_NATIVE_ARCH
    "Optimize Release builds for the host CPU (-march=native); disables portable codegen"
    OFF)

if(MSVC)
    add_compile_options(
        /W4
        /utf-8
        /MP          # parallel compilation
        /bigobj      # heavy Qt/QML template instantiation
    )
else()
    # GNU/Clang: senior-level warnings.
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )

    # Config-specific optimization levels (MSVC uses its own defaults).
    add_compile_options(
        "$<$<CONFIG:Debug>:-g3;-O0>"
        "$<$<CONFIG:Release>:-O3>"
    )
    if(CHADVIS_NATIVE_ARCH)
        add_compile_options("$<$<CONFIG:Release>:-march=native>")
    endif()
endif()

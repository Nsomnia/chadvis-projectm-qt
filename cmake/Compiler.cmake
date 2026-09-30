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

# ---------------------------------------------------------------------------
# Opt-in sanitizer lane.
#
# NOT a target. `unit_tests` links libproject_lib.a, which holds every line of
# application code (cmake/TargetSetup.cmake). A sanitizer applied only to a test
# target would link uninstrumented application objects, report nothing, and exit
# 0 -- a false-negative generator that is worse than having no lane at all. These
# flags are therefore directory-global, which in turn means this option is meant
# to be paired with a SEPARATE build directory:
#
#   cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#         -DCHADVIS_SANITIZER=thread
#   cmake --build build-tsan
#   ctest --test-dir build-tsan/tests --output-on-failure
#
# Use RelWithDebInfo, not Release: Release sets NDEBUG, which compiles out the
# Q_ASSERT thread-affinity guards in Playlist and LyricsSync -- exactly the
# fail-loud instrumentation a race hunt wants. The optimization level itself does
# not matter; -O3 detects races as reliably as -O0.
#
# Measured on Apple clang 16 with Homebrew Qt 6.11.1: -fsanitize=thread compiles,
# links (the driver auto-bakes LC_RPATH to libclang_rt.tsan_osx_dynamic.dylib, so
# no DYLD_LIBRARY_PATH is needed), runs, and detects a deliberate race.
#
# Do NOT switch to Homebrew LLVM for this. Its Darwin TSan runtime is absent from
# the Homebrew build and the link fails with an undefined reference to
# ___tsan_init. Stay on /usr/bin/c++.
# ---------------------------------------------------------------------------
set(CHADVIS_SANITIZER "" CACHE STRING "Sanitizer to build with: '', thread, address, undefined")
set_property(CACHE CHADVIS_SANITIZER PROPERTY STRINGS "" thread address undefined)

if(CHADVIS_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR
            "CHADVIS_SANITIZER is not wired for MSVC. Use clang-cl, or build "
            "the sanitized lane with GCC/Clang.")
    endif()
    add_compile_options(-fsanitize=${CHADVIS_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${CHADVIS_SANITIZER})
    message(STATUS
        "Sanitizer lane: -fsanitize=${CHADVIS_SANITIZER} "
        "(pair this with a dedicated build directory, e.g. build-tsan)")
endif()

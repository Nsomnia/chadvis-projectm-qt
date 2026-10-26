# DependencyBootstrap.cmake - best-effort automatic installation of missing
# build dependencies through the host's native package manager.
#
# When -DCHADVIS_AUTO_INSTALL_DEPS=ON is passed at configure time, every
# dependency probe in cmake/Dependencies.cmake and cmake/FindProjectM4.cmake
# that comes up empty gets one attempt at installing the missing piece through
# the host's package manager (apt, dnf, pacman, Homebrew, MacPorts, vcpkg,
# winget, or Chocolatey -- whichever is present and has a mapping for that
# dependency). The probe then re-runs; if it still fails, the previous
# behaviour applies unchanged: a CPM source build where one exists
# (fmt, spdlog, tomlplusplus, projectM), otherwise FATAL_ERROR.
#
# Design notes, because this file touches the user's system:
#   * Default is OFF. Nothing here runs unless the user opts in.
#   * A failed install never fails the configure. The fallbacks still apply.
#   * On Unix the privileged managers (apt/dnf/pacman/port) run through
#     `sudo -n` (non-interactive). If passwordless sudo is unavailable and we
#     are not root, those managers are skipped with a warning rather than
#     hanging on a password prompt. Homebrew refuses sudo by design and runs
#     unprivileged; vcpkg/winget/choco run as the invoking user.
#   * -DCHADVIS_AUTO_INSTALL_DRY_RUN=ON prints the install commands instead of
#     running them.
#   * Package names are best-effort and degrade gracefully: after any install
#     the original probe re-runs, so a wrong or stale mapping just falls
#     through to the source build / FATAL_ERROR as if nothing was installed.
#
# Public API:
#   chadvis_try_install_dependency(<LOGICAL>)
#     Attempts installation of <LOGICAL> (e.g. FMT, TAGLIB, PROJECTM).
#     Sets CHADVIS_INSTALL_OK in the caller's scope to TRUE if at least one
#     package-manager command exited 0, FALSE otherwise (including when the
#     option is off). The caller is responsible for re-running its probe.

include_guard(GLOBAL)

option(CHADVIS_AUTO_INSTALL_DEPS
    "Automatically install missing build dependencies via the host package manager (may need sudo)"
    OFF)
option(CHADVIS_AUTO_INSTALL_DRY_RUN
    "Print the dependency-install commands instead of running them"
    OFF)

# ---------------------------------------------------------------------------
# Manager detection (runs once at include time)
# ---------------------------------------------------------------------------

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_CHADVIS_PLATFORM_ORDER VCPKG WINGET CHOCO)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # This project's own dev box prefers MacPorts, then Homebrew, then vcpkg
    # for C/C++ (see the "Packages" rule in AGENTS.md).
    set(_CHADVIS_PLATFORM_ORDER PORT BREW VCPKG)
else()
    set(_CHADVIS_PLATFORM_ORDER APT DNF PACMAN VCPKG BREW)
endif()

find_program(_CHADVIS_APT_EXE apt-get)
find_program(_CHADVIS_DNF_EXE dnf)
find_program(_CHADVIS_PACMAN_EXE pacman)
find_program(_CHADVIS_BREW_EXE brew)
find_program(_CHADVIS_PORT_EXE port)
find_program(_CHADVIS_WINGET_EXE winget)
find_program(_CHADVIS_CHOCO_EXE choco)
find_program(_CHADVIS_VCPKG_EXE vcpkg)
if(NOT _CHADVIS_VCPKG_EXE AND DEFINED ENV{VCPKG_ROOT})
    find_program(_CHADVIS_VCPKG_EXE vcpkg PATHS "$ENV{VCPKG_ROOT}" NO_DEFAULT_PATH)
endif()

set(_CHADVIS_MANAGERS "")
foreach(_mgr IN LISTS _CHADVIS_PLATFORM_ORDER)
    if(_mgr STREQUAL "APT" AND _CHADVIS_APT_EXE)
        list(APPEND _CHADVIS_MANAGERS APT)
    elseif(_mgr STREQUAL "DNF" AND _CHADVIS_DNF_EXE)
        list(APPEND _CHADVIS_MANAGERS DNF)
    elseif(_mgr STREQUAL "PACMAN" AND _CHADVIS_PACMAN_EXE)
        list(APPEND _CHADVIS_MANAGERS PACMAN)
    elseif(_mgr STREQUAL "BREW" AND _CHADVIS_BREW_EXE)
        list(APPEND _CHADVIS_MANAGERS BREW)
    elseif(_mgr STREQUAL "PORT" AND _CHADVIS_PORT_EXE)
        list(APPEND _CHADVIS_MANAGERS PORT)
    elseif(_mgr STREQUAL "VCPKG" AND _CHADVIS_VCPKG_EXE)
        list(APPEND _CHADVIS_MANAGERS VCPKG)
    elseif(_mgr STREQUAL "WINGET" AND _CHADVIS_WINGET_EXE)
        list(APPEND _CHADVIS_MANAGERS WINGET)
    elseif(_mgr STREQUAL "CHOCO" AND _CHADVIS_CHOCO_EXE)
        list(APPEND _CHADVIS_MANAGERS CHOCO)
    endif()
endforeach()
unset(_mgr)

# If the configure itself runs on a vcpkg toolchain, vcpkg is where the
# missing package belongs -- a distro copy next to it invites version skew.
if(_CHADVIS_VCPKG_EXE
   AND DEFINED CMAKE_TOOLCHAIN_FILE
   AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
    list(REMOVE_ITEM _CHADVIS_MANAGERS VCPKG)
    list(PREPEND _CHADVIS_MANAGERS VCPKG)
endif()

# ---------------------------------------------------------------------------
# Privilege handling
# ---------------------------------------------------------------------------
# System managers need root. We never prompt: `sudo -n` either works
# non-interactively or the privileged managers are skipped for this configure.

set(_CHADVIS_SUDO "")
set(_CHADVIS_NO_PRIVILEGED_MANAGERS FALSE)
if((CHADVIS_AUTO_INSTALL_DEPS OR CHADVIS_AUTO_INSTALL_DRY_RUN)
   AND NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    execute_process(
        COMMAND id -u
        OUTPUT_VARIABLE _chadvis_euid
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _chadvis_euid STREQUAL "0")
        find_program(_CHADVIS_SUDO_EXE sudo)
        set(_chadvis_sudo_ok FALSE)
        if(_CHADVIS_SUDO_EXE)
            execute_process(
                COMMAND ${_CHADVIS_SUDO_EXE} -n true
                RESULT_VARIABLE _chadvis_sudo_rc
                OUTPUT_QUIET ERROR_QUIET)
            if(_chadvis_sudo_rc EQUAL 0)
                set(_chadvis_sudo_ok TRUE)
            endif()
        endif()
        if(_chadvis_sudo_ok)
            set(_CHADVIS_SUDO ${_CHADVIS_SUDO_EXE})
        else()
            set(_CHADVIS_NO_PRIVILEGED_MANAGERS TRUE)
            message(WARNING
                "CHADVIS_AUTO_INSTALL_DEPS is ON, but this user has no "
                "passwordless sudo (and is not root). Privileged package "
                "managers (apt/dnf/pacman/port) will be skipped; unprivileged "
                "managers and source-build fallbacks still apply.")
        endif()
        unset(_chadvis_sudo_ok)
        unset(_chadvis_sudo_rc)
    endif()
    unset(_chadvis_euid)
endif()

# ---------------------------------------------------------------------------
# Package name table
# ---------------------------------------------------------------------------
# CHADVIS_PKGS_<LOGICAL>_<MANAGER>: ;-list of packages providing <LOGICAL>.
# Empty or unset means "this manager cannot supply it" -- the manager is
# skipped for that dependency. A stale name degrades gracefully because the
# probe re-runs after every install.

# --- Qt6 (6.8+ floor, see Dependencies.cmake) ---
# APT is deliberately empty: Debian/Ubuntu LTS Qt is older than the 6.8
# floor, so installing it would mutate the system and still fail the version
# check. vcpkg/winget/choco are empty: building Qt from source takes hours
# and the binary options are unreliable.
set(CHADVIS_PKGS_QT6_BREW "qt")
set(CHADVIS_PKGS_QT6_PORT
    "qt6-qtbase;qt6-qtdeclarative;qt6-qtmultimedia;qt6-qtsvg;qt6-qtimageformats;qt6-qtshadertools;qt6-qttranslations")
set(CHADVIS_PKGS_QT6_PACMAN
    "qt6-base;qt6-declarative;qt6-multimedia;qt6-svg;qt6-imageformats;qt6-shadertools;qt6-translations")
set(CHADVIS_PKGS_QT6_DNF
    "qt6-qtbase-devel;qt6-qtdeclarative-devel;qt6-qtmultimedia-devel;qt6-qtsvg-devel;qt6-qtimageformats-devel;qt6-qtshadertools-devel")

# --- OpenGL (the platform GL stack; managers only carry the Linux headers) ---
set(CHADVIS_PKGS_OPENGL_APT "libgl1-mesa-dev")
set(CHADVIS_PKGS_OPENGL_DNF "mesa-libGL-devel")
set(CHADVIS_PKGS_OPENGL_PACMAN "mesa")

# --- fmt ---
set(CHADVIS_PKGS_FMT_APT "libfmt-dev")
set(CHADVIS_PKGS_FMT_DNF "fmt-devel")
set(CHADVIS_PKGS_FMT_PACMAN "fmt")
set(CHADVIS_PKGS_FMT_BREW "fmt")
set(CHADVIS_PKGS_FMT_PORT "fmt")
set(CHADVIS_PKGS_FMT_VCPKG "fmt")

# --- spdlog ---
set(CHADVIS_PKGS_SPDLOG_APT "libspdlog-dev")
set(CHADVIS_PKGS_SPDLOG_DNF "spdlog-devel")
set(CHADVIS_PKGS_SPDLOG_PACMAN "spdlog")
set(CHADVIS_PKGS_SPDLOG_BREW "spdlog")
set(CHADVIS_PKGS_SPDLOG_PORT "spdlog")
set(CHADVIS_PKGS_SPDLOG_VCPKG "spdlog")

# --- tomlplusplus ---
set(CHADVIS_PKGS_TOMLPLUSPLUS_APT "libtomlplusplus-dev")
set(CHADVIS_PKGS_TOMLPLUSPLUS_DNF "tomlplusplus-devel")
set(CHADVIS_PKGS_TOMLPLUSPLUS_PACMAN "tomlplusplus")
set(CHADVIS_PKGS_TOMLPLUSPLUS_BREW "tomlplusplus")
set(CHADVIS_PKGS_TOMLPLUSPLUS_VCPKG "tomlplusplus")
# MacPorts carries no tomlplusplus port; the CPM fallback covers it.

# --- TagLib ---
set(CHADVIS_PKGS_TAGLIB_APT "libtag1-dev")
set(CHADVIS_PKGS_TAGLIB_DNF "taglib-devel")
set(CHADVIS_PKGS_TAGLIB_PACMAN "taglib")
set(CHADVIS_PKGS_TAGLIB_BREW "taglib")
set(CHADVIS_PKGS_TAGLIB_PORT "taglib")
set(CHADVIS_PKGS_TAGLIB_VCPKG "taglib")

# --- glm ---
set(CHADVIS_PKGS_GLM_APT "libglm-dev")
set(CHADVIS_PKGS_GLM_DNF "glm-devel")
set(CHADVIS_PKGS_GLM_PACMAN "glm")
set(CHADVIS_PKGS_GLM_BREW "glm")
set(CHADVIS_PKGS_GLM_PORT "glm")
set(CHADVIS_PKGS_GLM_VCPKG "glm")

# --- FFmpeg (libavcodec/format/util/swscale/swresample) ---
set(CHADVIS_PKGS_FFMPEG_APT
    "libavcodec-dev;libavformat-dev;libavutil-dev;libswscale-dev;libswresample-dev")
set(CHADVIS_PKGS_FFMPEG_DNF "ffmpeg-devel")
set(CHADVIS_PKGS_FFMPEG_PACMAN "ffmpeg")
set(CHADVIS_PKGS_FFMPEG_BREW "ffmpeg")
set(CHADVIS_PKGS_FFMPEG_PORT "ffmpeg")
set(CHADVIS_PKGS_FFMPEG_VCPKG "ffmpeg")
# Best-effort binary mappings; the probe re-runs, so a mapping without dev
# headers just falls through to the FATAL_ERROR as before.
set(CHADVIS_PKGS_FFMPEG_WINGET "Gyan.FFmpeg")
set(CHADVIS_PKGS_FFMPEG_CHOCO "ffmpeg")

# --- libavfilter (optional post-pass; most ffmpeg packages bundle it) ---
set(CHADVIS_PKGS_AVFILTER_APT "libavfilter-dev")

# --- projectM v4 ---
# Deliberately sparse: most distro packages are v3 (Debian/Ubuntu
# libprojectm-dev, Fedora projectM-devel, and Homebrew's projectm formula is
# pinned to 3.1.12 per docs/user/INSTALL.md). Installing v3 would satisfy
# nothing -- the probes look for v4 -- so those managers stay empty and the
# CPM source build remains the primary path.
set(CHADVIS_PKGS_PROJECTM_PACMAN "projectm")

# ---------------------------------------------------------------------------
# Install command construction
# ---------------------------------------------------------------------------

function(_chadvis_install_command MANAGER PKGS OUT_VAR)
    if(MANAGER STREQUAL "APT")
        if(_CHADVIS_SUDO)
            set(_cmd "${_CHADVIS_SUDO};apt-get;install;-y;${PKGS}")
        else()
            set(_cmd "apt-get;install;-y;${PKGS}")
        endif()
    elseif(MANAGER STREQUAL "DNF")
        if(_CHADVIS_SUDO)
            set(_cmd "${_CHADVIS_SUDO};dnf;install;-y;${PKGS}")
        else()
            set(_cmd "dnf;install;-y;${PKGS}")
        endif()
    elseif(MANAGER STREQUAL "PACMAN")
        if(_CHADVIS_SUDO)
            set(_cmd "${_CHADVIS_SUDO};pacman;-S;--noconfirm;--needed;${PKGS}")
        else()
            set(_cmd "pacman;-S;--noconfirm;--needed;${PKGS}")
        endif()
    elseif(MANAGER STREQUAL "BREW")
        # Homebrew refuses to run under sudo by design.
        set(_cmd "brew;install;${PKGS}")
    elseif(MANAGER STREQUAL "PORT")
        if(_CHADVIS_SUDO)
            set(_cmd "${_CHADVIS_SUDO};port;install;${PKGS}")
        else()
            set(_cmd "port;install;${PKGS}")
        endif()
    elseif(MANAGER STREQUAL "VCPKG")
        set(_cmd "${_CHADVIS_VCPKG_EXE};install;${PKGS}")
    elseif(MANAGER STREQUAL "WINGET")
        set(_cmd "winget;install;-e;--id;${PKGS};--silent;--accept-package-agreements;--accept-source-agreements")
    elseif(MANAGER STREQUAL "CHOCO")
        set(_cmd "choco;install;-y;${PKGS}")
    else()
        message(FATAL_ERROR "_chadvis_install_command: unknown manager '${MANAGER}'")
    endif()
    set(${OUT_VAR} "${_cmd}" PARENT_SCOPE)
endfunction()

function(_chadvis_apt_update_once)
    if(_CHADVIS_APT_UPDATED)
        return()
    endif()
    if(_CHADVIS_SUDO)
        set(_update_cmd "${_CHADVIS_SUDO};apt-get;update")
    else()
        set(_update_cmd "apt-get;update")
    endif()
    string(REPLACE ";" " " _pretty "${_update_cmd}")
    if(CHADVIS_AUTO_INSTALL_DRY_RUN)
        message(STATUS "[dry-run] would run: ${_pretty}")
    else()
        message(STATUS "Refreshing apt package lists: ${_pretty}")
        execute_process(
            COMMAND ${_update_cmd}
            RESULT_VARIABLE _rc
            TIMEOUT 300
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc EQUAL 0)
            message(STATUS "apt-get update failed (exit ${_rc}); continuing anyway")
        endif()
    endif()
    set(_CHADVIS_APT_UPDATED TRUE CACHE INTERNAL
        "DependencyBootstrap: apt-get update already ran this configure")
endfunction()

# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

function(chadvis_try_install_dependency LOGICAL)
    set(CHADVIS_INSTALL_OK FALSE PARENT_SCOPE)
    if(NOT CHADVIS_AUTO_INSTALL_DEPS AND NOT CHADVIS_AUTO_INSTALL_DRY_RUN)
        return()
    endif()

    set(_tried "")
    foreach(_mgr IN LISTS _CHADVIS_MANAGERS)
        # Privileged managers were settled at include time: without root or
        # passwordless sudo they are skipped, never prompted.
        if(_CHADVIS_NO_PRIVILEGED_MANAGERS
           AND (_mgr STREQUAL "APT" OR _mgr STREQUAL "DNF"
                OR _mgr STREQUAL "PACMAN" OR _mgr STREQUAL "PORT"))
            continue()
        endif()

        set(_varname "CHADVIS_PKGS_${LOGICAL}_${_mgr}")
        set(_pkgs "${${_varname}}")
        if(_pkgs STREQUAL "")
            continue()
        endif()

        if(_mgr STREQUAL "APT")
            _chadvis_apt_update_once()
        endif()

        _chadvis_install_command(${_mgr} "${_pkgs}" _cmd)
        string(REPLACE ";" " " _pretty "${_cmd}")
        if(CHADVIS_AUTO_INSTALL_DRY_RUN)
            message(STATUS "[dry-run] would run: ${_pretty}")
            set(CHADVIS_INSTALL_OK TRUE PARENT_SCOPE)
            return()
        endif()

        message(STATUS "Installing '${LOGICAL}' via ${_mgr}: ${_pretty}")
        execute_process(
            COMMAND ${_cmd}
            RESULT_VARIABLE _rc
            TIMEOUT 1800
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err)
        if(_rc EQUAL 0)
            message(STATUS "'${LOGICAL}' installed via ${_mgr}")
            set(CHADVIS_INSTALL_OK TRUE PARENT_SCOPE)
            return()
        endif()
        message(STATUS
            "Install of '${LOGICAL}' via ${_mgr} failed (exit ${_rc}); "
            "trying next package manager")
        list(APPEND _tried ${_mgr})
    endforeach()

    if(_tried STREQUAL "")
        message(STATUS
            "No package manager available with a mapping for '${LOGICAL}'; "
            "falling back to the previous behaviour")
    else()
        message(STATUS
            "All package managers failed for '${LOGICAL}' (tried: ${_tried}); "
            "falling back to the previous behaviour")
    endif()
endfunction()

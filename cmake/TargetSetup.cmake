# TargetSetup.cmake - common include/link assembly and target definitions:
#   project_lib          static library holding all engine/UI/QML-bridge code
#   ChadVis QML module   attached to project_lib (URI ChadVis, NO_PLUGIN)
#   chadvis-projectm-qt  main executable

# Common include directories.
# NOTE: spdlog/fmt/tomlplusplus are consumed via CMake targets (find_package/
# CPM), so no pkg-config-style variables exist for them. ProjectM headers
# arrive transitively through PROJECTM_LINK_TARGETS; PROJECTM_INCLUDE_DIRS is
# only non-empty in the pkg-config fallback path.
#
# DO NOT "FIX" THE MISSING /usr/local/include BY ADDING IT HERE (2026-09-29).
# spdlog, fmt, toml++, glm and taglib are all Homebrew CONFIG packages whose
# INTERFACE_INCLUDE_DIRECTORIES is exactly /usr/local/include, and they are
# already linked via CPM_LIBS below — so the directory really does propagate.
# It is absent from build/compile_commands.json anyway, because it is entry #1
# of CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES (build/CMakeFiles/*/
# CMakeCXXCompiler.cmake) and CMake elides include dirs the compiler already
# searches. Re-adding it here would be elided identically. Forcing it on with
# add_compile_options would reach the command line, but it would land in
# CXX_FLAGS — after every -I in CXX_INCLUDES and below Qt's -isystem paths —
# demoting the only route to those five packages in a build that is correct
# today. The real compiler gets the directory from its own driver, which is
# also why no third-party tool reading this database can see it; see the
# measured note in .clang-tidy.
set(COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/qml_bridge
    ${GLM_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${PROJECTM_INCLUDE_DIRS}
    ${readerwriterqueue_SOURCE_DIR}
)

# Common link libraries. projectM links via proper targets on every platform
# (imported targets from the system install, or ALIAS targets from the CPM
# source build); raw flags only in the Linux pkg-config fallback.
set(COMMON_LIBS
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Sql
    ${TAGLIB_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${PROJECTM_LINK_TARGETS}
    ${PROJECTM_LINK_FLAGS}
    OpenGL::GL
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

# pffft used to be appended here. It was the only unconditional entry in this
# list -- the three above are each guarded by an `if(TARGET ...)` system-first
# probe -- so this line was the sole reason a configure step fetched over the
# network for a library with no consumer. The empty list case is therefore no
# longer reachable while spdlog/fmt/tomlplusplus resolve by either route, but
# `set(CPM_LIBS "")` above already handles it rather than leaving an undefined
# name in target_link_libraries.

# ---------------------------------------------------------------------------
# Static library with all application code
# ---------------------------------------------------------------------------

add_library(project_lib STATIC
    ${UTIL_SOURCES}
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${VISUALIZER_SOURCES}
    ${SUNO_SOURCES}
    ${SUNO_AUTH_SOURCES}
    ${RECORDER_SOURCES}
    ${LYRICS_SOURCES}
    ${CHADVIS_POSTPROCESS_SOURCES}
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

# Compile-time only: src/pch.hpp carries the Qt/std headers every TU re-parses.
# Measured on the reference box (2 physical cores), the parse of those headers is
# the bulk of a TU's wall clock, and ~350 TUs pay it independently. Nothing here
# changes what a TU means -- the header holds includes and nothing else.
# Opt out with -DCHADVIS_NO_PCH=ON when bisecting a build oddity.
option(CHADVIS_NO_PCH "Build without the shared precompiled header" OFF)
if(NOT CHADVIS_NO_PCH)
    target_precompile_headers(project_lib PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.hpp)
endif()

# The version is a build-time fact: version.txt at the repo root is validated
# into PROJECT_VERSION by the root CMakeLists. PUBLIC so every consumer of
# project_lib (the executable, the test lanes) reports the identical value;
# the user config is deliberately never consulted for it.
target_compile_definitions(project_lib PUBLIC CHADVIS_VERSION="${PROJECT_VERSION}")

# PUBLIC so the test lanes, which link project_lib, see the same answer the
# library was built with. A test that quietly compiled the burn-in assertions
# against a build that has no burn-in would be the exact silent-no-op this
# feature is supposed to avoid.
if(CHADVIS_HAS_AVFILTER)
    target_compile_definitions(project_lib PUBLIC CHADVIS_HAS_AVFILTER=1)
endif()

# Same reasoning for the encrypted-playback crypto: PUBLIC so the test lanes
# compile their assertions against the same answer the library was built with.
if(CHADVIS_HAVE_AVAES_INIT)
    target_compile_definitions(project_lib PUBLIC CHADVIS_HAVE_AVAES_INIT=1)
endif()

# ─────────────────────────────────────────────────────────────
# QML MODULE - Modern UI components
# ─────────────────────────────────────────────────────────────

    qt_policy(SET QTP0001 NEW)
    # NOTE: QTP0004 is deliberately NOT enabled. It requires a qmldir file for
    # every directory containing QML and remaps the resource layout, which
    # breaks relative sibling imports (e.g. settings/AccountPage.qml importing
    # "./AccountSessionCard.qml" resolved to "no such directory" at runtime).
    # The configure-time author warning is the correct trade here; revisit only
    # together with real per-directory qmldir files.

    # Mark QML singletons before qt_add_qml_module
set_source_files_properties(src/qml/styles/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Palette data for the theme engine. Both singletons are auto-registered with the
# ChadVis module, so QML reaches them by name with no import path change.
set_source_files_properties(src/qml/styles/ThemeDefinitions.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(project_lib
    URI ChadVis
    VERSION 1.0
    QML_FILES ${QML_SOURCES}
    SOURCES ${QML_BRIDGE_SOURCES}
    RESOURCES
        resources/icons/play.svg
        resources/icons/pause.svg
        resources/icons/stop.svg
        resources/icons/next.svg
        resources/icons/prev.svg
        resources/icons/record.svg
        resources/icons/shuffle.svg
        resources/icons/expand.svg
        resources/icons/volume-high.svg
        resources/icons/volume-mute.svg
        resources/icons/plus.svg
        resources/icons/clear.svg
        resources/icons/star-filled.svg
        resources/icons/star-outline.svg
        resources/icons/delete.svg
        resources/icons/random.svg
        resources/icons/blacklist.svg
        resources/icons/qml/playback.svg
        resources/icons/qml/playlist.svg
        resources/icons/qml/presets.svg
        resources/icons/qml/lyrics.svg
        resources/icons/qml/suno.svg
        resources/icons/qml/overlay.svg
        resources/icons/qml/recording.svg
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/ChadVis
    NO_PLUGIN
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------

add_executable(chadvis-projectm-qt src/main.cpp)
# CHADVIS_VERSION arrives via project_lib's PUBLIC definition above; redefining
# it here would be a second copy of the same value to keep in sync.
target_link_libraries(chadvis-projectm-qt PRIVATE project_lib)

# ---------------------------------------------------------------------------
# macOS application bundle
# ---------------------------------------------------------------------------

if(APPLE)
    # Without MACOSX_BUNDLE the target is a bare Mach-O executable: no
    # Contents/, no Info.plist, no Frameworks/, and no way for the Finder, the
    # Dock, `open`, notifications or the crash reporter to identify it. CPack's
    # DragNDrop generator would still have produced a .dmg, but one containing a
    # loose executable plus whatever dylibs happened to sit in bin/ -- an archive
    # that cannot be launched and is not an application. Setting this only under
    # APPLE is deliberate: MACOSX_BUNDLE is an Apple-only property and CMake
    # warns when it is set on a target for a non-Apple platform.
    #
    # TARGET_NAME is NOT set by add_executable (verified on CMake 4.4: a probe
    # project prints TARGET_NAME=[] after add_executable), so the executable's
    # on-disk name is read back from the target instead of assumed. CFBundleExecutable
    # and the file in Contents/MacOS must agree exactly or `open` calls the bundle
    # corrupt; OUTPUT_NAME is consulted first because CMake renames the file after
    # it when it is set, and the target name is the fallback when it is not.
    # PREFIX/SUFFIX are deliberately not consulted: neither is set on this target,
    # and a target property that exists but was never used is not a reason to
    # carry the guesswork of handling it.
    get_target_property(CHADVIS_BUNDLE_EXECUTABLE chadvis-projectm-qt OUTPUT_NAME)
    if(NOT CHADVIS_BUNDLE_EXECUTABLE)
        get_target_property(CHADVIS_BUNDLE_EXECUTABLE chadvis-projectm-qt NAME)
    endif()

    # Version is interpolated into the plist from PROJECT_VERSION, which the root
    # CMakeLists derives from version.txt. There is deliberately no second copy
    # of the version in this file or in the plist template.
    #
    # LSMinimumSystemVersion is the one floor worth getting right, and it is not
    # a free choice: a floor LOWER than the linked Qt's is a bundle that
    # advertises systems on which dyld cannot load it. Measured on this machine
    # (Qt 6.11.1, Homebrew, Command Line Tools SDK 15.2):
    #     otool -l <qt>/lib/QtCore.framework/Versions/A/QtCore | grep -A4 LC_BUILD_VERSION
    #       platform 1 / minos 14.0 / sdk 15.2
    # ...which is also what the current, unbundled binary already reports, so
    # adopting 14.0 narrows nothing. An explicit -DCMAKE_OSX_DEPLOYMENT_TARGET
    # wins, because a distribution builder who sets it has measured their own
    # floor and this file has no business second-guessing it.
    set(CHADVIS_MACOS_MIN_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}" CACHE STRING
        "Minimum macOS version recorded as LSMinimumSystemVersion. Defaults to CMAKE_OSX_DEPLOYMENT_TARGET, then to the measured floor of the linked Qt.")
    if(NOT CHADVIS_MACOS_MIN_VERSION)
        set(CHADVIS_MACOS_MIN_VERSION "14.0")
    endif()

    # One identifier, two consumers: the plist template (so the generated plist is
    # valid read on its own) and MACOSX_BUNDLE_GUI_IDENTIFIER (which is what CMake
    # writes into CFBundleIdentifier and what Qt reads for QSettings/
    # QStandardPaths scoping). Reverse-DNS lower case, the App Store's rule.
    set(CHADVIS_BUNDLE_IDENTIFIER "com.chadvis.app" CACHE STRING
        "CFBundleIdentifier / reverse-DNS identifier for the macOS bundle")

    # configure_file substitutes an unknown @VAR@ with the empty string and does
    # not say so. An empty CFBundleExecutable produces a bundle that cpack
    # packages, Finder lists, and `open` refuses to launch -- so the one class of
    # typo this template invites is checked here instead of being discovered by a
    # user. (This is the same shape as the second lesson in TODO.md's recording
    # round: a comment that states a belief is not a check that enforces it.)
    foreach(_chadvis_plist_var IN ITEMS
            CHADVIS_BUNDLE_EXECUTABLE
            CHADVIS_BUNDLE_IDENTIFIER
            CHADVIS_MACOS_MIN_VERSION
            PROJECT_VERSION)
        if("${${_chadvis_plist_var}}" STREQUAL "")
            message(FATAL_ERROR
                "cmake/Info.plist.in interpolates @${_chadvis_plist_var}@ but that "
                "variable is empty, which would emit an Info.plist that macOS "
                "silently rejects.")
        endif()
    endforeach()
    unset(_chadvis_plist_var)

    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        "${CMAKE_BINARY_DIR}/Info.plist"
        @ONLY)

    set_target_properties(chadvis-projectm-qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
        # What this property actually does, measured rather than assumed, because
        # the obvious reading of it is wrong: it does NOT name the bundle
        # directory. With MACOSX_BUNDLE_INFO_PLIST supplying a plist, the built
        # and installed bundles are both named after the TARGET
        # (chadvis-projectm-qt.app), in the build tree and in the install tree
        # alike -- verified by ls of both. This property feeds CFBundleName, and
        # with a supplied plist CMake left the template's own values in place, so
        # the shipped plist reads:
        #     CFBundleName        ChadVis
        #     CFBundleDisplayName ChadVis
        # It is set to "ChadVis" and not to "ChadVis.app" precisely because the
        # first is a plausible CFBundleName and the second is not: an ".app"
        # suffix in CFBundleName is what a misreading of this property looks
        # like, and it is the one value here that would ship.
        MACOSX_BUNDLE_BUNDLE_NAME "ChadVis"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${CHADVIS_BUNDLE_IDENTIFIER}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # The executable lives at ChadVis.app/Contents/MacOS/chadvis-projectm-qt,
        # so one level up and across is Contents/Frameworks -- where both
        # macdeployqt's Qt frameworks and this target's own copied dylibs land.
        # This is also what makes the BUILD-TREE bundle runnable, because the root
        # CMakeLists sets CMAKE_BUILD_WITH_INSTALL_RPATH ON, so the binary is
        # linked with this RPATH rather than the build-tree search path.
        INSTALL_RPATH "@executable_path/../Frameworks"
    )

    # Deployment is suppressed in the fast-iteration profile, so this bundle has
    # no Contents/Frameworks -- and the shipped rpath above then points at a
    # directory that does not exist. That used to be survivable because
    # Homebrew's Qt frameworks carry absolute install names, so dyld never needed
    # an rpath at all; qt.io's carry @rpath/..., and the build-tree app dies on
    # "Library not loaded: @rpath/QtQuick.framework/... no LC_RPATH's found".
    # Point it at the Qt that built it, for this profile only: a shipped bundle
    # must not carry the developer's Qt path.
    if(CHADVIS_FAST_ITERATION AND QT6_INSTALL_PREFIX)
        set_property(TARGET chadvis-projectm-qt APPEND PROPERTY
            INSTALL_RPATH "${QT6_INSTALL_PREFIX}/lib")
    endif()

    # -------------------------------------------------------------------------
    # Bundle deployment: framework + dylib closure, then macdeployqt, then sign.
    #
    # The measured starting point, because "make it a bundle" is not the same
    # problem as "make it relocatable". `otool -L` on the pre-change binary:
    #
    #   * LC_RPATH: NONE AT ALL. `otool -l` has no `cmd LC_RPATH` entry.
    #   * Every single dependency is an ABSOLUTE path into Homebrew:
    #       /usr/local/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore
    #       /usr/local/opt/ffmpeg/lib/libavformat.63.dylib
    #       /usr/local/opt/taglib/lib/libtag.2.dylib  (+ spdlog, fmt, toml++)
    #
    # So the .dmg CPack could already produce was a Mach-O that runs on exactly
    # one machine -- the one it was built on, with Homebrew installed. Two tools
    # fix that, in this order, and the order is the whole point:
    #
    #   1. chadvis-deploy-dylibs (generated below). Rewrites the app and every
    #      NON-Qt dylib in its transitive closure to @rpath and copies them into
    #      Contents/Frameworks. FFmpeg is the reason this exists and it is not
    #      small: libavfilter alone pulls in libass, freetype, fontconfig,
    #      harfbuzz, and Homebrew's FFmpeg 9.0.2 links x264, x265, aom, svt-av1,
    #      dav1d, libvpx, libvmaf, lame, opus, theora, ogg, vorbis, lzma, snappy
    #      and libX11. None of that is Qt's business and macdeployqt will not
    #      touch it.
    #   2. macdeployqt. The only thing that knows how to copy a Qt *framework*
    #      with its Versions/ hierarchy, the plugins (platforms/qcocoa,
    #      imageformats, and the multimedia backends the app needs to decode
    #      audio at all) and their own dependencies, and to rewrite their install
    #      names. Hand-rolling that with $<TARGET_RUNTIME_DLLS> produces a bundle
    #      that links and then shows a blank window, which is why it is not used.
    #      It must run AFTER step 1: it sees @rpath references that already
    #      resolve inside the bundle and leaves them alone, instead of trying to
    #      deploy libraries that are already deployed.
    #   3. codesign. Last, always, because steps 1 and 2 both rewrite load
    #      commands and every rewrite invalidates the code signature of the file
    #      it was applied to -- including Homebrew's own ad-hoc signatures on the
    #      dylibs copied in step 1. See the block below.
    #
    # What is NOT here, and does not need to be: the QML module, the .qrc styles
    # and the module's icons are all COMPILED INTO THE EXECUTABLE, not loaded
    # from disk. Measured, not assumed: the linked binary carries
    # qInitResources_qmake_ChadVis, qInitResources_project_lib_raw_qml_0 and
    # qInitResources_project_lib_raw_res_0, and src/core/Application.cpp loads
    # `qrc:/qt/qml/ChadVis/src/qml/main.qml`. So there is no QML search path to
    # get wrong at runtime and nothing QML-shaped to install into the bundle --
    # the "blank window because the module is not installed" failure mode is
    # structurally impossible for this project, and installing the .qml files as
    # well would be a second copy of a resource that is already inside the binary.
    # -------------------------------------------------------------------------

    # Deployment has its own FORCE flag rather than sniffing whether the user
    # named CHADVIS_DEPLOY_BUNDLE. Sniffing cannot work: option() writes the cache
    # entry on the FIRST configure, so every re-configure of that directory reads
    # back "defined" and concludes the user asked for it. That turned the fast
    # profile's suppression into dead code from the second configure onward, and
    # ./build.sh --fast paid the full 5-7 minute deploy on every relink.
    option(CHADVIS_DEPLOY_BUNDLE
        "Populate the macOS bundle with its Qt frameworks, plugins and dylib closure (macdeployqt). OFF yields a bare executable that only runs on the build machine"
        ON)
    option(CHADVIS_FORCE_DEPLOY
        "Run the bundle deployment even in the fast-iteration profile"
        OFF)

    # Measured cost of the three deploy steps below, on this machine: 5-7 minutes
    # per link of the app target (42 dylibs copied and rewritten, 12 Qt
    # frameworks, 62 QML module qmldirs, then a full-tree codesign). That is
    # irrelevant for a release build and ruinous for the edit/test loop, where
    # changing one .cpp relinks the app and nothing else.
    #
    # So the fast-iteration profile is suppressed -- but only when the user has
    # not asked for it by name, which is the whole point of the _named flag above.
    # It does not opt out of the BUNDLE, only of filling it, and that is safe for
    # its purpose: with deployment skipped the executable keeps the absolute
    # Homebrew load paths it has always had and runs on the machine that built it,
    # the only machine ./build.sh --fast is ever used on. Nothing that ships goes
    # through this profile; build.sh maps it to Debug + CHADVIS_FAST_ITERATION=ON.
    set(_chadvis_do_deploy ${CHADVIS_DEPLOY_BUNDLE})
    if(_chadvis_do_deploy AND NOT CHADVIS_FORCE_DEPLOY
       AND CHADVIS_FAST_ITERATION AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_chadvis_do_deploy OFF)
        message(STATUS
            "macOS bundle: deployment SUPPRESSED for the fast-iteration profile "
            "(-O0 -g0, Debug). The .app still builds and still runs on this "
            "machine, but it carries no frameworks and is not shippable -- use a "
            "Release build for that. Force deployment with "
            "-DCHADVIS_FORCE_DEPLOY=ON.")
    endif()

    # macdeployqt is found by name because Qt 6 does not export it as a CMake
    # target: `grep -rl macdeployqt <qt>/lib/cmake` returns nothing on the Qt
    # 6.11.1 this project builds against. Prefer the versioned name, fall back to
    # the unversioned one, then to the Qt prefix that was just located.
    if(NOT CHADVIS_MACDEPLOYQT)
        get_filename_component(_chadvis_qt_prefix "${Qt6Core_DIR}/../../.." ABSOLUTE)
        find_program(CHADVIS_MACDEPLOYQT
            NAMES macdeployqt6 macdeployqt
            HINTS "${_chadvis_qt_prefix}/bin"
            DOC "Path to macdeployqt; a bundle cannot be made self-contained without it")
        unset(_chadvis_qt_prefix)
    endif()

    if(_chadvis_do_deploy)
        if(NOT CHADVIS_MACDEPLOYQT)
            # Loud, because the alternative is a .dmg that builds cleanly, ships
            # cleanly, and dies on first launch on every machine but this one --
            # with a dyld error that points at Qt rather than at the packaging.
            message(FATAL_ERROR
                "CHADVIS_DEPLOY_BUNDLE is ON but macdeployqt was not found. Without "
                "it the .app cannot carry the Qt frameworks and plugins it needs, so "
                "the bundle would run only where Homebrew Qt happens to be "
                "installed. Install the Qt deployment tools (macOS: "
                "'brew install qt' ships them), point CMake at them with "
                "-DCHADVIS_MACDEPLOYQT=/path/to/macdeployqt, or pass "
                "-DCHADVIS_DEPLOY_BUNDLE=OFF to accept a non-relocatable bundle.")
        endif()

        # Written at configure time and run with `cmake -P` from the POST_BUILD
        # chain below. A CMake script rather than a shell script because the
        # deployment is a graph walk (a work list with a visited set and one
        # install_name_tool call per Mach-O) and this way it stays on the same
        # toolchain as everything else in the build.
        #
        # Two stages, one file. STAGE=deploy runs before macdeployqt and owns the
        # non-framework closure; STAGE=verify runs after it and owns the claim
        # that the finished bundle has no absolute load path left. The second
        # half is the part that matters: a bundle that only launches on the build
        # machine is not a fix, it is a different failure, and nothing about it is
        # visible until someone else tries to open the .dmg.
        set(CHADVIS_DEPLOY_DYLIB_SCRIPT "${CMAKE_BINARY_DIR}/chadvis-deploy-dylibs.cmake")
        file(WRITE "${CHADVIS_DEPLOY_DYLIB_SCRIPT}" [==[
# Generated by cmake/TargetSetup.cmake -- do not edit, do not commit.
#
# STAGE=deploy  copy the non-framework Mach-O closure into Contents/Frameworks
#               and rewrite every reference to @rpath/<soname>
# STAGE=verify  every non-system dependency of the executable is bundle-relative
#               AND resolves to a file that exists inside the bundle
#
# Required: STAGE, EXECUTABLE, FRAMEWORKS_DIR.

foreach(_required IN ITEMS STAGE EXECUTABLE FRAMEWORKS_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} was not passed to the deploy script")
    endif()
endforeach()

# otool output split on newlines leaves a trailing empty element, and the
# default list() policy warns about (and silently ignores) it once per Mach-O
# -- hundreds of dev warnings in a CI log. NEW keeps the element and the
# explicit REMOVE_ITEM below makes the index arithmetic deterministic.
if(POLICY CMP0007)
    cmake_policy(SET CMP0007 NEW)
endif()

# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------

# "System" means dyld resolves it without any RPATH, or it is already
# bundle-relative. Everything else must end up inside Contents/Frameworks.
function(_is_system_dep dep out)
    if(dep MATCHES "^@" OR dep MATCHES "^/usr/lib/" OR dep MATCHES "^/System/"
       OR dep MATCHES "^/Library/Frameworks/")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# A framework is a directory bundle: lifting QtCore.framework/Versions/A/QtCore
# out as a bare file called QtCore yields a framework whose own dependencies
# still point outside it. macdeployqt owns frameworks and plugins, so they are
# reported here and deployed there.
function(_is_framework_dep dep out)
    if(dep MATCHES "\\.framework/")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# otool
# ---------------------------------------------------------------------------

# Direct dependencies of one Mach-O, as a list, with the "(compatibility
# version ...)" tail otool appends stripped off.
function(_deps_of macho out)
    execute_process(
        COMMAND otool -L "${macho}"
        OUTPUT_VARIABLE _raw
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "otool -L failed on ${macho}: ${_err}")
    endif()

    # otool -L prints the file's OWN install name as the first dependency line,
    # so a framework whose LC_ID is still the vendor's absolute path reports
    # that path as a dependency of itself. That is metadata, not a load, and
    # treating it as a hole made the macOS gate fail on macdeployqt's own
    # output (measured on macos-15, 2026-10-24: twenty Qt frameworks each
    # "depending" on /opt/homebrew/opt/<formula>/lib/<itself>). otool -D is the
    # tool that distinguishes the two.
    set(_self_id "")
    execute_process(
        COMMAND otool -D "${macho}"
        OUTPUT_VARIABLE _id_raw
        RESULT_VARIABLE _id_rc
        ERROR_QUIET)
    if(_id_rc EQUAL 0)
        string(REPLACE "\n" ";" _id_lines "${_id_raw}")
        list(REMOVE_ITEM _id_lines "")
        list(LENGTH _id_lines _id_count)
        if(_id_count GREATER 1)
            list(GET _id_lines 1 _self_id)
            string(STRIP "${_self_id}" _self_id)
        endif()
    endif()

    set(_list "")
    string(REPLACE "\n" ";" _lines "${_raw}")
    foreach(_line IN LISTS _lines)
        # Indented lines are dependencies; the first line is the file itself.
        if(_line MATCHES "^[ \t]")
            string(REGEX REPLACE "^[ \t]+" "" _dep "${_line}")
            string(REGEX REPLACE " *\\(compatibility version.*$" "" _dep "${_dep}")
            if(_dep AND (_self_id STREQUAL "" OR NOT _dep STREQUAL _self_id))
                list(APPEND _list "${_dep}")
            endif()
        endif()
    endforeach()
    set(${out} "${_list}" PARENT_SCOPE)
endfunction()

# Does @rpath/<rel> or @executable_path/<rel> name a file that exists?
function(_resolves_in_bundle dep out)
    get_filename_component(_macos_dir "${EXECUTABLE}" DIRECTORY)
    if(dep MATCHES "^@rpath/(.*)$")
        set(_candidate "${FRAMEWORKS_DIR}/${CMAKE_MATCH_1}")
    elseif(dep MATCHES "^@executable_path/(.*)$")
        set(_candidate "${_macos_dir}/${CMAKE_MATCH_1}")
    elseif(dep MATCHES "^@loader_path/(.*)$")
        get_filename_component(_loader_dir "${macho}" DIRECTORY)
        set(_candidate "${_loader_dir}/${CMAKE_MATCH_1}")
    else()
        set(_candidate "")
    endif()
    if(_candidate AND EXISTS "${_candidate}")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# LC_RPATH entries of one Mach-O, with @loader_path/@executable_path expanded so
# the caller can test them with EXISTS.
function(_rpaths_of macho out)
    get_filename_component(_macho_dir "${macho}" DIRECTORY)
    get_filename_component(_exe_dir "${EXECUTABLE}" DIRECTORY)
    execute_process(COMMAND otool -l "${macho}" OUTPUT_VARIABLE _raw RESULT_VARIABLE _rc)
    set(_list "")
    if(_rc EQUAL 0)
        string(REPLACE "\n" ";" _lines "${_raw}")
        set(_in FALSE)
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "cmd LC_RPATH")
                set(_in TRUE)
            elseif(_in AND _line MATCHES "path (.*) \\(offset")
                set(_p "${CMAKE_MATCH_1}")
                string(REPLACE "@loader_path" "${_macho_dir}" _p "${_p}")
                string(REPLACE "@executable_path" "${_exe_dir}" _p "${_p}")
                list(APPEND _list "${_p}")
                set(_in FALSE)
            endif()
        endforeach()
    endif()
    set(${out} "${_list}" PARENT_SCOPE)
endfunction()

# Every Mach-O this project placed in the bundle: the executable, the flat
# dylibs, and each framework's versioned binary (never the `Current` symlink).
function(_bundle_machos out)
    file(GLOB _flat "${FRAMEWORKS_DIR}/*.dylib")
    file(GLOB _versioned "${FRAMEWORKS_DIR}/*.framework/Versions/*/*")
    set(_list "${EXECUTABLE}")
    set(_seen "")
    foreach(_candidate IN LISTS _flat _versioned)
        if(IS_DIRECTORY "${_candidate}")
            continue()
        endif()
        # Versions/Current is a DIRECTORY symlink to Versions/A, so
        # Versions/Current/QtCore is the same file as Versions/A/QtCore and
        # globbing both would report every finding twice.
        get_filename_component(_real "${_candidate}" REALPATH)
        list(FIND _seen "${_real}" _slot)
        if(_slot EQUAL -1)
            list(APPEND _seen "${_real}")
            list(APPEND _list "${_candidate}")
        endif()
    endforeach()
    set(${out} "${_list}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# STAGE=verify
# ---------------------------------------------------------------------------

if(STAGE STREQUAL "verify")
    # Every Mach-O the bundle carries, not just the executable. The narrower
    # version passed a bundle whose libbrotlidec could not find libbrotlicommon
    # (2026-10-23): the executable's own load commands were clean and the app
    # died on launch, because the missing reference lived one level down. A gate
    # that only inspects the top of the graph is not a gate.
    _bundle_machos(_verify_machos)
    set(_bad "")
    foreach(_macho IN LISTS _verify_machos)
        _deps_of("${_macho}" _all)
        foreach(_dep IN LISTS _all)
            _is_system_dep("${_dep}" _sys)
            if(NOT _sys)
                # An absolute non-system path means the bundle is only runnable on
                # the machine that built it. This is the failure the whole exercise
                # exists to prevent, so it stops the build instead of shipping.
                list(APPEND _bad "${_macho} -> ${_dep} (absolute)")
                continue()
            endif()
            if(NOT _dep MATCHES "^@")
                continue()  # a system path, already cleared above
            endif()
            _resolves_in_bundle("${_dep}" _ok)
            if(NOT _ok)
                list(APPEND _bad "${_macho} -> ${_dep} (no such file in the bundle)")
            endif()
        endforeach()
    endforeach()

    # An RPATH is what makes @rpath mean anything. Its absence is silent at
    # runtime on the build machine, where the absolute paths are still present.
    execute_process(
        COMMAND otool -l "${EXECUTABLE}"
        OUTPUT_VARIABLE _loadcmds
        RESULT_VARIABLE _rc)
    set(_rpaths "")
    if(_rc EQUAL 0)
        string(REPLACE "\n" ";" _lc_lines "${_loadcmds}")
        set(_in_rpath FALSE)
        foreach(_lc IN LISTS _lc_lines)
            if(_lc MATCHES "cmd LC_RPATH")
                set(_in_rpath TRUE)
            elseif(_lc MATCHES "path ")
                if(_in_rpath)
                    string(REGEX REPLACE "^[ \t]*path (.*) \\(offset.*$" "\\1" _p "${_lc}")
                    list(APPEND _rpaths "${_p}")
                endif()
                set(_in_rpath FALSE)
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES _rpaths)
    list(FIND _rpaths "@executable_path/../Frameworks" _rpath_index)
    if(_rpath_index EQUAL -1)
        list(APPEND _bad "no LC_RPATH of @executable_path/../Frameworks")
    endif()

    if(_bad)
        string(REPLACE ";" "\n    " _pretty "${_bad}")
        message(FATAL_ERROR
            "The bundle is NOT relocatable; ${EXECUTABLE}:\n    ${_pretty}\n"
            "An .app with these load paths opens on the build machine and nowhere "
            "else. Fix the packaging rather than shipping the .dmg.")
    endif()
    message(STATUS
        "Bundle verified: no absolute non-system load paths, "
        "LC_RPATH @executable_path/../Frameworks present")
    return()
endif()

# ---------------------------------------------------------------------------
# STAGE=fill
#
# Whatever macdeployqt could not resolve, this stage resolves. Measured
# 2026-10-23 on the first Release build against the qt.io SDK: macdeployqt
# copied Contents/Frameworks/libbrotlidec.1.dylib and reported
#
#   ERROR: Cannot resolve rpath "@rpath/libbrotlicommon.1.dylib"
#
# and the bundle it had just declared deployed died on launch with
#
#   Library not loaded: @rpath/libbrotlicommon.1.dylib
#
# while the build exited 0. A dependency of a library macdeployqt placed is
# still a dependency of the bundle, and nothing looked at it: STAGE=deploy runs
# BEFORE macdeployqt (so it never sees what macdeployqt introduces), and
# STAGE=verify only inspected the executable's own load commands.
#
# The rule: an @rpath reference that does not resolve inside the bundle is
# resolved against the referencing Mach-O's own LC_RPATH entries, then a short
# list of local prefixes, copied into Contents/Frameworks, given a bundle id,
# and the bundle is re-scanned until nothing changes. Framework references are
# NOT filled here -- a framework is a directory bundle and macdeployqt owns
# those; an unresolvable one is a hard error rather than a half-copied file.
# ---------------------------------------------------------------------------

if(STAGE STREQUAL "fill")
    # Where a missing library may legitimately live on the build machine. The
    # referencing Mach-O's own rpaths come first and cover the common case; the
    # fixed list covers a dependency whose rpath was stripped before it reached
    # us; the two globs cover a keg-only Homebrew formula, which keeps its
    # libraries in .../opt/<formula>/lib rather than in any prefix on the path
    # (measured on macos-15, 2026-10-24: QtCore needs libicuuc.78.dylib, which
    # lives only in /opt/homebrew/opt/icu4c/lib).
    set(_preferred "/usr/local/lib" "/opt/local/lib" "/opt/homebrew/lib")
    foreach(_pass RANGE 0 8)
        set(_changed FALSE)
        _bundle_machos(_fill_machos)
        foreach(_macho IN LISTS _fill_machos)
            _deps_of("${_macho}" _deps)
            _rpaths_of("${_macho}" _rpaths)
            foreach(_dep IN LISTS _deps)
                # Three shapes reach a hole, and all three are the same problem:
                # the reference does not resolve inside the bundle. @rpath is
                # macdeployqt's usual output, @loader_path is how a copied
                # library addresses its own siblings (libicuuc from libicui18n),
                # and an absolute path is what a vendor leaves in an install
                # name it never rewrote.
                if(_dep MATCHES "^@rpath/(.*)$")
                    set(_rel "${CMAKE_MATCH_1}")
                    set(_absolute FALSE)
                elseif(_dep MATCHES "^@(loader|executable)_path/(.*)$")
                    set(_rel "${CMAKE_MATCH_2}")
                    set(_absolute FALSE)
                elseif(_dep MATCHES "^/")
                    set(_rel "")
                    set(_absolute TRUE)
                else()
                    continue()
                endif()
                _resolves_in_bundle("${_dep}" _ok)
                if(_ok)
                    continue()
                endif()
                if(_absolute)
                    _is_system_dep("${_dep}" _sys)
                    if(_sys)
                        continue()
                    endif()
                endif()

                get_filename_component(_base "${_dep}" NAME)
                if(_rel MATCHES "\\.framework/" OR _base MATCHES "\\.framework$")
                    message(FATAL_ERROR
                        "${_macho} references ${_dep}, a framework the bundle does "
                        "not carry and this stage will not fake by copying one "
                        "binary out of it. macdeployqt owns frameworks.")
                endif()

                set(_found "")
                # An absolute reference still names its own source.
                if(_absolute AND EXISTS "${_dep}")
                    get_filename_component(_found "${_dep}" REALPATH)
                endif()
                # A sibling of the referrer, which is where @loader_path points.
                if(NOT _found)
                    get_filename_component(_here "${_macho}" DIRECTORY)
                    if(EXISTS "${_here}/${_base}")
                        get_filename_component(_found "${_here}/${_base}" REALPATH)
                    endif()
                endif()
                # The referrer's own rpaths, then the prefixes above.
                if(NOT _found)
                    foreach(_dir IN LISTS _rpaths _preferred)
                        if(EXISTS "${_dir}/${_base}")
                            get_filename_component(_found "${_dir}/${_base}" REALPATH)
                            break()
                        endif()
                    endforeach()
                endif()
                if(NOT _found)
                    file(GLOB _keg "/opt/homebrew/opt/*/lib/${_base}"
                         "/usr/local/opt/*/lib/${_base}"
                         "/opt/local/lib/${_base}")
                    if(_keg)
                        list(GET _keg 0 _found)
                        get_filename_component(_found "${_found}" REALPATH)
                    endif()
                endif()
                if(NOT _found)
                    message(FATAL_ERROR
                        "Cannot resolve '${_dep}' referenced by ${_macho}: it is not "
                        "in the bundle, not beside its referrer, not in any of "
                        "${_rpaths};${_preferred}, and not in a Homebrew keg. The "
                        "bundle would launch on this machine and fail on every "
                        "other one.")
                endif()

                # Name the copy by the reference itself. Naming it after the
                # source's realpath basename would rewrite the dependency to a
                # different soname (libbrotlicommon.1.dylib ->
                # libbrotlicommon.1.2.0.dylib) and make a second run -- or a
                # bundle macdeployqt already filled -- look like a name collision
                # between two libraries. A reference with a path component is the
                # exception; there the basename is the only sane destination and
                # the reference is rewritten below.
                set(_soname "${_rel}")
                if(_soname STREQUAL "" OR _soname MATCHES "/")
                    set(_soname "${_base}")
                endif()
                if(EXISTS "${FRAMEWORKS_DIR}/${_soname}")
                    # Already there from an earlier pass or from macdeployqt. The
                    # reference resolves through it, which is the only property
                    # this stage promises; a differing file under the same name
                    # would be dyld's ambiguity to resolve, not ours to guess at.
                    message(STATUS "  ${_soname} already present for ${_macho}")
                else()
                    # Only a real copy counts as progress, or the pass loop never
                    # terminates on a bundle that already has the file.
                    set(_changed TRUE)
                    configure_file("${_found}" "${FRAMEWORKS_DIR}/${_soname}" COPYONLY)
                    file(CHMOD "${FRAMEWORKS_DIR}/${_soname}"
                         PERMISSIONS
                             OWNER_READ OWNER_WRITE OWNER_EXECUTE
                             GROUP_READ GROUP_EXECUTE
                             WORLD_READ WORLD_EXECUTE)
                    execute_process(
                        COMMAND install_name_tool -id "@rpath/${_soname}"
                                "${FRAMEWORKS_DIR}/${_soname}"
                        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
                    if(NOT _rc EQUAL 0)
                        message(FATAL_ERROR "install_name_tool -id failed on ${_soname}: ${_err}")
                    endif()
                    message(STATUS "  filled ${_soname} (from ${_found}) for ${_macho}")
                endif()
                # Every filled reference ends up bundle-relative, whatever shape
                # it arrived in; that is the property the verify stage checks.
                if(NOT _dep STREQUAL "@rpath/${_soname}")
                    set(_changed TRUE)
                    execute_process(
                        COMMAND install_name_tool -change "${_dep}" "@rpath/${_soname}"
                                "${_macho}"
                        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
                    if(NOT _rc EQUAL 0)
                        message(FATAL_ERROR
                            "install_name_tool -change failed on ${_macho}: ${_err}")
                    endif()
                endif()
            endforeach()
        endforeach()
        if(NOT _changed)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "Filling the bundle did not converge in 9 passes; the dependency graph is "
        "cycling or growing on every pass.")
endif()

# ---------------------------------------------------------------------------
# STAGE=deploy
#
# Work list rather than recursion: every piece of state below is a plain
# top-scope variable, so there is no PARENT_SCOPE to get wrong and a cycle in
# the dependency graph is a visited-set hit instead of a stack overflow.
# ---------------------------------------------------------------------------

if(NOT STAGE STREQUAL "deploy")
    message(FATAL_ERROR "Unknown STAGE '${STAGE}' (expected deploy or verify)")
endif()

file(MAKE_DIRECTORY "${FRAMEWORKS_DIR}")

set(_seen "")          # realpaths already copied this run
set(_copied "")        # sonames copied, for the closing summary
set(_deployed "")      # "soname=<n>;realpath=<p>" per placed library
set(_frameworks "")    # framework deps, left to macdeployqt
set(_worklist "${EXECUTABLE}")
set(_checked "")

message(STATUS "Deploying the non-Qt dylib closure into ${FRAMEWORKS_DIR}")

while(_worklist)
    # Both arguments are variable NAMES. Quoting "${_worklist}" here would make
    # CMake look for a list variable literally named after the path, pop nothing,
    # and spin forever -- which is what it did the first time.
    list(POP_FRONT _worklist _macho)
    _deps_of("${_macho}" _deps)
    set(_args "")
    foreach(_dep IN LISTS _deps)
        _is_system_dep("${_dep}" _sys)
        if(_sys)
            continue()
        endif()
        _is_framework_dep("${_dep}" _fw)
        if(_fw)
            list(APPEND _frameworks "${_dep}")
            continue()
        endif()

        if(NOT EXISTS "${_dep}")
            # Loud: a path that does not exist on this machine cannot be copied
            # into the bundle, so the result would be a .app that fails to
            # launch with a dyld error naming a file nobody shipped.
            message(FATAL_ERROR
                "Dependency '${_dep}' of ${_macho} does not exist on this "
                "machine, so it cannot be deployed into the bundle.")
        endif()

        get_filename_component(_soname "${_dep}" NAME)

        get_filename_component(_soname "${_dep}" NAME)

        # Realpath is the identity key on purpose. Homebrew links FFmpeg's own
        # libraries through the stable /usr/local/opt/... symlinks while the
        # libraries reference each other through versioned /usr/local/Cellar/...
        # paths, so the same file arrives under two different strings; keying on
        # the string would ship libavcodec twice.
        get_filename_component(_real "${_dep}" REALPATH)

        # Has this SONAME already been placed during this run? A second library
        # claiming a name that is taken is a hard error, because the only way to
        # satisfy both is to ship one of them and hope. Compared as strings
        # because a deployed copy's realpath is the copy, never the source.
        string(FIND "${_deployed}" "soname=${_soname};" _slot)
        if(_slot EQUAL -1)
            configure_file("${_real}" "${FRAMEWORKS_DIR}/${_soname}" COPYONLY)
            # configure_file(COPYONLY) leaves the copy read-only (444), which is
            # enough to link against and not enough for the NEXT stage to touch:
            # measured, macdeployqt strips every library it finds in
            # Contents/Frameworks and answers "file is not writable (Permission
            # denied)" for each one. 755 matches the mode the library already had
            # where it came from, and is what a dylib in a bundle conventionally
            # carries.
            file(CHMOD "${FRAMEWORKS_DIR}/${_soname}"
                 PERMISSIONS
                     OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE
                     WORLD_READ WORLD_EXECUTE)
            list(APPEND _seen "${_real}")
            list(APPEND _copied "${_soname}")
            list(APPEND _deployed "soname=${_soname};realpath=${_real}")

            # Give the copy a bundle-relative id so nothing can keep looking for
            # it at the Homebrew path.
            #
            # NOT passing -headerpad_max_install_names, which is what a build
            # engineer reaches for here and which does not work on this
            # toolchain: measured, the CommandLineTools install_name_tool
            # rejects it with "more than one input file specified (... and
            # -headerpad_max_install_names)" both before and after the mode
            # option, and its own usage string does not list it. It is also not
            # needed here: every path rewritten below is strictly SHORTER than the
            # absolute Homebrew path it replaces (`/usr/local/Cellar/taglib/
            # 2.3.2/lib/libtag.2.3.2.dylib` -> `@rpath/libtag.2.dylib`), and the
            # failure mode when that assumption breaks is install_name_tool
            # refusing with "larger updated load commands do not fit" and naming
            # the file -- which is a better answer than a silently ignored flag.
            execute_process(
                COMMAND install_name_tool -id "@rpath/${_soname}"
                        "${FRAMEWORKS_DIR}/${_soname}"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR "install_name_tool -id failed on ${_soname}: ${_err}")
            endif()

            list(APPEND _worklist "${FRAMEWORKS_DIR}/${_soname}")
        else()
            string(REGEX MATCH "soname=${_soname};realpath=[^;]*" _entry "${_deployed}")
            if(NOT _entry MATCHES "realpath=${_real}$")
                message(FATAL_ERROR
                    "Two different libraries want '${_soname}' in "
                    "Contents/Frameworks: ${_real} and the one already placed "
                    "there (${_entry}). Shipping either silently ships the wrong "
                    "library.")
            endif()
        endif()

        list(APPEND _args -change "${_dep}" "@rpath/${_soname}")
    endforeach()

    if(_args)
        execute_process(
            COMMAND install_name_tool ${_args} "${_macho}"
            RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "install_name_tool failed on ${_macho}: ${_err}")
        endif()
    endif()
    list(APPEND _checked "${_macho}")
endwhile()

if(_frameworks)
    list(REMOVE_DUPLICATES _frameworks)
    message(STATUS
        "  ${_frameworks} left to macdeployqt, which owns framework bundles")
endif()

# Self-check the closure that was just built. Every non-framework, non-system
# dependency of everything this stage touched -- the executable and every copied
# dylib -- must now be @rpath AND resolve to a file inside Contents/Frameworks.
set(_unresolved "")
foreach(_macho IN LISTS _checked)
    _deps_of("${_macho}" _deps)
    foreach(_dep IN LISTS _deps)
        _is_system_dep("${_dep}" _sys)
        if(_sys)
            continue()
        endif()
        _is_framework_dep("${_dep}" _fw)
        if(_fw)
            continue()  # macdeployqt's stage; STAGE=verify checks it afterwards
        endif()
        _resolves_in_bundle("${_dep}" _ok)
        if(NOT _ok)
            list(APPEND _unresolved "${_macho} -> ${_dep}")
        endif()
    endforeach()
endforeach()
if(_unresolved)
    string(REPLACE ";" "\n    " _pretty "${_unresolved}")
    message(FATAL_ERROR "Dylib deployment left unresolved references:\n    ${_pretty}")
endif()

list(LENGTH _copied _copied_count)
message(STATUS "Deployed ${_copied_count} non-Qt dylib(s) into Contents/Frameworks")
]==])

    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -DSTAGE=deploy
                -DEXECUTABLE=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/MacOS/$<TARGET_FILE_BASE_NAME:chadvis-projectm-qt>
                -DFRAMEWORKS_DIR=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/Frameworks
                -P "${CHADVIS_DEPLOY_DYLIB_SCRIPT}"
        COMMENT "Deploying non-Qt dylib closure into the macOS bundle"
        VERBATIM)

    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CHADVIS_MACDEPLOYQT}"
                "$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>"
                # A deploy tool that silently copies fewer frameworks than needed
                # is worse than one that says so, and macdeployqt's own output is
                # the only inventory of what it believes the app needs.
                -verbose=1
                # -qmldir is NOT optional here, and leaving it off produces a
                # bundle that starts, logs three warnings and shows no window:
                #
                #   QQmlApplicationEngine failed to load component
                #   QML Warning: module "QtQuick.Controls" plugin
                #                 "qtquickcontrols2plugin" not found (main.qml:18)
                #   Failed to create QML window
                #
                # The reason is that macdeployqt discovers which QML modules to
                # deploy by PARSING .qml FILES ON DISK, and this project's .qml
                # files are not on disk: qt_add_qml_module compiles them into the
                # executable's resource system (qInitResources_qmake_ChadVis is
                # linked in, and src/core/Application.cpp loads
                # qrc:/qt/qml/ChadVis/src/qml/main.qml). It also writes
                # Contents/Resources/qt.conf with `Imports = Resources/qml`, which
                # is what turns the omission into a hard failure: Qt stops
                # resolving modules against the build machine's Qt install and
                # looks only inside the bundle, where nothing was deployed. So the
                # binary works on the machine that built it and fails everywhere
                # else -- which is the whole failure mode this file exists to
                # remove, one layer deeper than the dylibs.
                #
                # src/qml is the directory to hand it: every entry in
                # QML_SOURCES (cmake/Sources.cmake) lives under it. Enumerated
                # from those files rather than guessed -- `import QtQuick` (58),
                # `import ChadVis` (57, our own module: C++-registered singletons
                # plus resource-embedded QML, so it needs no deployment),
                # `QtQuick.Layouts` (51), `QtQuick.Controls` (38),
                # `QtQuick.Effects` (6), `QtQuick.Dialogs` (5),
                # `QtQuick.Window` (2), `Qt.labs.platform` (1).
                -qmldir=${CMAKE_SOURCE_DIR}/src/qml
        COMMENT "Deploying Qt frameworks, plugins and QML modules (${CHADVIS_MACDEPLOYQT})"
        VERBATIM)

    # Resolve what macdeployqt could not. It runs between deployment and the
    # verify gate, because its whole input is what macdeployqt placed: measured
    # 2026-10-23, macdeployqt copied libbrotlidec and reported "Cannot resolve
    # rpath @rpath/libbrotlicommon.1.dylib", the bundle exited the build
    # successfully, and it died on launch with exactly that dyld error. A
    # dependency of a deployed library is a dependency of the bundle.
    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -DSTAGE=fill
                -DEXECUTABLE=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/MacOS/$<TARGET_FILE_BASE_NAME:chadvis-projectm-qt>
                -DFRAMEWORKS_DIR=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/Frameworks
                -P "${CHADVIS_DEPLOY_DYLIB_SCRIPT}"
        COMMENT "Filling unresolved @rpath references left by macdeployqt"
        VERBATIM)

    # The check that turns "I think this is relocatable" into a fact. It runs
    # after macdeployqt, so it sees the Qt frameworks too, and it fails the build
    # rather than warning: an .app that opens on the build machine and nowhere
    # else is precisely the trap this whole block exists to remove.
    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -DSTAGE=verify
                -DEXECUTABLE=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/MacOS/$<TARGET_FILE_BASE_NAME:chadvis-projectm-qt>
                -DFRAMEWORKS_DIR=$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>/Contents/Frameworks
                -P "${CHADVIS_DEPLOY_DYLIB_SCRIPT}"
        COMMENT "Verifying the bundle is relocatable (no absolute load paths)"
        VERBATIM)
endif()  # _chadvis_do_deploy

# ---------------------------------------------------------------------------
# Code signing -- an inert hook, and deliberately the LAST thing that touches
# the bundle.
#
# What is wired: one identity, from -DCHADVIS_CODESIGN_IDENTITY=... or the
# environment variable of the same name, and nothing else. There is no keychain
# lookup, no certificate discovery, no profile download, no Apple ID, and no
# network access of any kind. An empty identity means ad-hoc ("-"), which needs
# no credentials at all and is what macdeployqt already does by default.
#
# Why ad-hoc rather than skipping the step entirely when no identity is set:
# both earlier deployment steps rewrite load commands, and every load-command
# rewrite invalidates the code signature of the file it was applied to -- the
# copied Homebrew dylibs arrive already signed. An app whose nested code has a
# broken signature is refused by dyld on Apple Silicon, so "skip signing" would
# mean deliberately shipping a bundle that cannot run on the platform most of
# this project's users are on. Ad-hoc signing is credential-free and local; it
# asserts nothing about provenance.
#
# NOTARIZATION IS NOT ATTEMPTED HERE AND CANNOT BE. Notarization needs an Apple
# Developer account, credentials that must never live in a build script or a
# CI log, and a stapled ticket that is served by Apple's CDN -- so it is a
# deliberate manual step run after cpack, with a developer identity, on a machine
# that can reach Apple. Anything this file invented to fake that would be worse
# than having none. To distribute: build, cpack, codesign with a real identity
# (set CHADVIS_CODESIGN_IDENTITY), then `xcrun notarytool submit` + `stapler
# staple` by hand.
#
# --deep is used, and it is the tool of last resort by Apple's own guidance.
# A production signing pipeline signs nested code explicitly, innermost first.
# The reason it is acceptable here is that the identity is opt-in and the tree
# being signed was assembled by the two steps above in this same file, so there
# is no third-party bundle with an unexpected nested layout. A signing pipeline
# added later should drop --deep and walk Contents/Frameworks itself.
# ---------------------------------------------------------------------------
set(CHADVIS_CODESIGN_IDENTITY "$ENV{CHADVIS_CODESIGN_IDENTITY}" CACHE STRING
    "codesign identity for the macOS bundle ('-' for ad-hoc, a Developer ID for distribution). Empty means ad-hoc")

if(CHADVIS_CODESIGN_IDENTITY STREQUAL "")
    set(_chadvis_sign_identity "-")
    set(_chadvis_sign_extra "")
    set(_chadvis_sign_note "ad-hoc (no identity set)")
else()
    set(_chadvis_sign_identity "${CHADVIS_CODESIGN_IDENTITY}")
    # --options runtime is the hardened runtime, which notarization requires and
    # which the un-hardened default would silently fail to satisfy. --timestamp
    # has to travel with a real identity; the timestamp server rejects ad-hoc.
    set(_chadvis_sign_extra --options runtime --timestamp)
    set(_chadvis_sign_note "'${_chadvis_sign_identity}' (hardened runtime, timestamped)")
endif()

# Qt finds a bundle's plugins by asking CoreFoundation for the main bundle, and
# CoreFoundation only recognises a directory as a bundle when the path component
# ends in `.app`. Rename the copied bundle -- `ChadVis-copy`, `.app-latest`, a
# staging directory -- and the app dies at startup with "Could not find the Qt
# platform plugin cocoa in \"\"", which names neither the cause nor the fix.
# `qt.conf` states the plugin path instead of inferring it, so a renamed bundle
# still runs. Relative paths in qt.conf are resolved against Contents/MacOS.
#
# It must be installed BEFORE the codesign step below: adding a file to a signed
# bundle invalidates its signature, and dyld refuses broken signatures on Apple
# Silicon.
#
# Only when deployment actually happened. `Plugins = ../PlugIns` points at a
# directory macdeployqt creates, so writing it into a bare fast-iteration bundle
# tells Qt to stop using the build machine's plugins and look in a directory that
# is not there -- "Could not load the Qt platform plugin cocoa in ''".
if(_chadvis_do_deploy)
    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle-qt.conf"
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Resources/qt.conf"
        COMMENT "Installing qt.conf (plugin path survives a renamed bundle)"
        VERBATIM)
else()
    # A suppressed build relinks against the build machine's absolute Homebrew
    # paths, so a bundle left over from an earlier deployed build must go: its
    # frameworks and plugins are found through qt.conf and @rpath, which loads a
    # SECOND Qt into the process. The symptom is four "implemented in both"
    # objc warnings and then a qFatal in the cocoa plugin, which reads like a
    # code bug and is stale packaging. Resources/qml goes with them: macdeployqt
    # fills it with the Qt module tree plus symlinks into PlugIns, so leaving it
    # behind leaves ~2 000 dead files and 60 dangling links that make `cp -r` of
    # the bundle fail for reasons unrelated to the build.
    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/PlugIns"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Resources/qml"
        COMMAND "${CMAKE_COMMAND}" -E rm -f
                "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Resources/qt.conf"
        COMMENT "macOS bundle: deployment suppressed, bundle left bare"
        VERBATIM)
endif()

add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
    COMMAND codesign --force --deep --sign "${_chadvis_sign_identity}"
            ${_chadvis_sign_extra}
            "$<TARGET_BUNDLE_DIR:chadvis-projectm-qt>"
    COMMENT "Code-signing the macOS bundle: ${_chadvis_sign_note}"
    VERBATIM)

message(STATUS
    "macOS bundle: ${CHADVIS_BUNDLE_IDENTIFIER} ${PROJECT_VERSION}, "
    "LSMinimumSystemVersion ${CHADVIS_MACOS_MIN_VERSION}, "
    "signing ${_chadvis_sign_note}")

unset(_chadvis_sign_identity)
unset(_chadvis_sign_extra)
unset(_chadvis_sign_note)

endif()  # APPLE

# ---------------------------------------------------------------------------
# Auth subsystem: OS keychain linkage (src/suno/auth/CredentialStore).
#
# macOS uses Security.framework generic-password items; every other platform
# (or a build with CHADVIS_NO_KEYCHAIN=ON) uses the documented plaintext-file
# fallback with 0600 permissions.
# ---------------------------------------------------------------------------

option(CHADVIS_NO_KEYCHAIN
    "Disable OS keychain secret storage; force the file-backed fallback" OFF)

if(APPLE AND NOT CHADVIS_NO_KEYCHAIN)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    target_link_libraries(project_lib PUBLIC ${SECURITY_FRAMEWORK})
    target_compile_definitions(project_lib PUBLIC CHADVIS_HAS_KEYCHAIN)
endif()

# Windows and Linux get a real backend instead of a plaintext file. Without
# this, CHADVIS_HAS_KEYCHAIN was undefined on both and every platform fell
# through to FileBackend, which writes the Clerk credential -- whose
# token_type is "refresh" with Max-Age=31536000, i.e. a ONE-YEAR secret in the
# clear. The macro guards the *inside* of each backend file rather than
# excluding the file, so both always compile and each defines its factory in
# every configuration (returning nullptr when compiled out). That is what lets
# CredentialStore.cpp dispatch with no platform #ifdef of its own, and it is
# why the file is listed unconditionally in cmake/Sources.cmake.
if(WIN32 AND NOT CHADVIS_NO_KEYCHAIN)
    target_compile_definitions(project_lib PUBLIC CHADVIS_HAS_WIN32_CREDENTIALS)
endif()

if(UNIX AND NOT APPLE AND NOT CHADVIS_NO_KEYCHAIN)
    # QtDBus, not libsecret: libsecret spins a GMainContext in a Qt app whose
    # entire event model is Qt's, and every caller here is already on
    # CredentialStoreWorker. The API actually needed is five methods.
    target_link_libraries(project_lib PUBLIC Qt6::DBus)
    target_compile_definitions(project_lib PUBLIC CHADVIS_HAS_SECRET_SERVICE)
endif()

# ---------------------------------------------------------------------------
# Resampler engine (soxr) -- OPTIONAL, and reported rather than enforced.
#
# `swr_set_engine` does not exist. Measured on the libswresample this project
# links (7.1.102): the header declares `enum SwrEngine {SWR_ENGINE_SWR,
# SWR_ENGINE_SOXR, ...}` but exports no function of that name -- `nm -gU` lists
# twenty swr_ symbols and none is it. Engine selection is an AVOption:
# `av_opt_set(ctx, "engine", "soxr", 0)`.
#
# The option is OMITTED ENTIRELY when libswresample was built without
# --enable-libsoxr, and that cannot be detected at configure time. So having
# libsoxr installed says nothing about whether the *linked* FFmpeg can use it,
# and a compile-time gate on pkg-config would be a lie in both directions.
# src/recorder/ResamplerEngine.cpp therefore probes at runtime and degrades
# with a reason; this block only reports.
#
# Nothing is linked: libswresample pulls in libsoxr itself, and this must never
# reach CHADVIS_FFMPEG_COMPONENTS, which FATAL_ERRORs on a missing component --
# that would turn an optional audio-quality flag into a build failure of the
# whole application.
# ---------------------------------------------------------------------------
option(CHADVIS_USE_SOXR_ENGINE
    "Ask libswresample for its higher-quality (soxr) resampling engine where the linked FFmpeg was built with --enable-libsoxr; degrades to libswresample's own resampler otherwise"
    ON)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(SOXR QUIET soxr)
endif()
if(SOXR_FOUND)
    message(STATUS
        "Resampler: libsoxr ${SOXR_VERSION} is installed, so the soxr engine MAY be "
        "usable. That does NOT confirm the linked FFmpeg was configured with "
        "--enable-libsoxr; compare with -DCHADVIS_USE_SOXR_ENGINE=0.")
else()
    message(STATUS
        "Resampler: libsoxr NOT found. Not an error: the runtime probe will keep "
        "libswresample's own resampler and log the reason once. Homebrew ships "
        "`sox`, which is a different library -- libsoxr comes from your FFmpeg "
        "vendor's package.")
endif()
if(NOT CHADVIS_USE_SOXR_ENGINE)
    message(STATUS
        "Resampler: soxr disabled by CHADVIS_USE_SOXR_ENGINE=0 -- libswresample's "
        "own resampler only, which is the A/B baseline")
    target_compile_definitions(project_lib PUBLIC CHADVIS_USE_SOXR_ENGINE=0)
endif()

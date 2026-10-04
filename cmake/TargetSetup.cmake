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

list(APPEND CPM_LIBS PFFFT::PFFFT)

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

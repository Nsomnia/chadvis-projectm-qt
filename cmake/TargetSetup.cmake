# TargetSetup.cmake - common include/link assembly and target definitions:
#   project_lib          static library holding all engine/UI/QML-bridge code
#   ChadVis QML module   attached to project_lib (URI ChadVis, NO_PLUGIN)
#   chadvis-projectm-qt  main executable

# Common include directories.
# NOTE: spdlog/fmt/tomlplusplus are consumed via CMake targets (find_package/
# CPM), so no pkg-config-style variables exist for them. ProjectM headers
# arrive transitively through PROJECTM_LINK_TARGETS; PROJECTM_INCLUDE_DIRS is
# only non-empty in the pkg-config fallback path.
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

# ─────────────────────────────────────────────────────────────
# QML MODULE - Modern UI components
# ─────────────────────────────────────────────────────────────

qt_policy(SET QTP0001 NEW)

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
target_link_libraries(chadvis-projectm-qt PRIVATE project_lib)

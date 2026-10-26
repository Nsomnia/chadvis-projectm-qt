# PresetPack.cmake -- the visualizer's preset library.
#
# projectM ships no presets. Without a pack the canvas can only ever draw
# projectM's own built-in idle preset, which makes the renderer and therefore
# every recording one picture. So the build fetches the pack the projectM
# project itself calls its default -- Jason Fletcher's "cream of the crop",
# https://github.com/projectM-visualizer/presets-cream-of-the-crop -- and the
# runtime finds it through file::presetsDir() (src/util/FileUtils.cpp).
#
# Why a configure-time fetch instead of a vendored tree or a runtime download:
#
#   - vendoring ~350 .milk files in this repository makes every clone heavier for
#     a library that is not source code and is not ours to maintain;
#   - a runtime download needs a ZIP reader (Qt has none public), a progress UI
#     and a failure story, for a pack the installer can simply carry.
#
# Three rules this file obeys:
#
#   1. **Offline is not a build failure.** No network, a 404, a timeout or a
#      truncated zip all leave CHADVIS_BUNDLED_PRESET_DIR empty with a warning;
#      the app then shows the idle preset exactly as it does today.
#   2. **Never into the source tree** (T0089): the zip and the extracted tree live
#      under the build directory, which is disposable.
#   3. **The pack's own licence travels with it.** The extracted tree keeps
#      LICENSE.md, which is installed next to the presets.

option(CHADVIS_BUNDLE_PRESETS
    "Fetch the projectM 'cream of the crop' preset pack into the build and install it with the app"
    ON)

set(CHADVIS_BUNDLED_PRESET_DIR "" CACHE INTERNAL
    "Root of the bundled preset pack, empty when no pack is available")

if(NOT CHADVIS_BUNDLE_PRESETS)
    message(STATUS "Preset pack: disabled by CHADVIS_BUNDLE_PRESETS=OFF -- the visualizer will show only projectM's idle preset")
    return()
endif()

set(_chadvis_preset_url
    "https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/zip/refs/heads/master")
set(_chadvis_preset_root "${CMAKE_BINARY_DIR}/_deps/presets-cream-of-the-crop-master")
set(_chadvis_preset_zip "${CMAKE_BINARY_DIR}/_deps/presets-cream-of-the-crop.zip")

# One probe, not a re-download: a pack that is already extracted is the normal
# case on every configure after the first, and re-fetching 15 MB per configure
# would make the fast loop depend on the network.
if(NOT EXISTS "${_chadvis_preset_root}/LICENSE.md")
    message(STATUS "Preset pack: fetching ${_chadvis_preset_url}")
    file(DOWNLOAD "${_chadvis_preset_url}" "${_chadvis_preset_zip}"
        STATUS _chadvis_preset_status
        TIMEOUT 180
        TLS_VERIFY ON)

    list(GET _chadvis_preset_status 0 _chadvis_preset_code)
    list(GET _chadvis_preset_status 1 _chadvis_preset_text)
    if(NOT _chadvis_preset_code EQUAL 0)
        message(WARNING
            "Preset pack: download failed (${_chadvis_preset_code} ${_chadvis_preset_text}). "
            "The build continues and the visualizer will show only projectM's idle preset. "
            "Re-run configure with network access, or -DCHADVIS_BUNDLE_PRESETS=OFF to silence this.")
        file(REMOVE "${_chadvis_preset_zip}")
        return()
    endif()

    file(ARCHIVE_EXTRACT INPUT "${_chadvis_preset_zip}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    file(REMOVE "${_chadvis_preset_zip}")

    if(NOT EXISTS "${_chadvis_preset_root}/LICENSE.md")
        message(WARNING
            "Preset pack: the archive did not contain a presets-cream-of-the-crop-master tree; "
            "the visualizer will show only projectM's idle preset.")
        return()
    endif()
endif()

set(CHADVIS_BUNDLED_PRESET_DIR "${_chadvis_preset_root}" CACHE INTERNAL
    "Root of the bundled preset pack, empty when no pack is available")
message(STATUS "Preset pack: ${CHADVIS_BUNDLED_PRESET_DIR}")

# The build tree is where a dev binary runs from, so the definition is what makes
# a `./build.sh --fast` app find the same pack the installed one carries. PUBLIC
# only because project_lib is the target every translation unit links through.
target_compile_definitions(project_lib PRIVATE
    CHADVIS_BUNDLED_PRESET_DIR="${CHADVIS_BUNDLED_PRESET_DIR}")

# Everything below ships the pack: Linux/Windows read it from share/, macOS from
# inside the .app, because a .dmg install is a drag of the bundle to
# /Applications and a sibling share/ tree would be left behind.
install(DIRECTORY "${CHADVIS_BUNDLED_PRESET_DIR}/"
    DESTINATION share/chadvis-projectm-qt/presets)

if(APPLE)
    add_custom_command(TARGET chadvis-projectm-qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CHADVIS_BUNDLED_PRESET_DIR}"
            "$<TARGET_BUNDLE_CONTENT_DIR:chadvis-projectm-qt>/Resources/presets"
        COMMENT "Copying the preset pack into the app bundle"
        VERBATIM)
endif()

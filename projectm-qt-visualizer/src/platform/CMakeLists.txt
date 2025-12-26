# Platform-specific library
add_library(platform_lib INTERFACE)

# Linux-specific: PulseAudio/PipeWire audio capture
if(UNIX AND NOT APPLE)
    # PulseAudio is checked in main CMakeLists
    if(PULSEAUDIO_FOUND)
        target_sources(platform_lib INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/PulseAudioSource.cpp
        )
        target_include_directories(platform_lib INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${PULSEAUDIO_INCLUDE_DIRS}
        )
        # Need both pulse and pulse-simple for pa_simple_* functions
        target_link_libraries(platform_lib INTERFACE
            pulse
            pulse-simple
        )
    endif()
endif()

add_library(audio_lib STATIC
SilentAudioSource.cpp
# Add more as implemented
)
target_include_directories(audio_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(audio_lib PUBLIC
Qt6::Core
Qt6::Multimedia
)

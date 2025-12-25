# Platform-specific library
# Currently placeholder
add_library(platform_lib INTERFACE)
# Future Linux-specific:
# if(UNIX AND NOT APPLE)
# target_sources(platform_lib PRIVATE
# linux/PulseAudioSource.cpp
# )
# endif()

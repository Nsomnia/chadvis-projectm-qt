add_library(projectm_lib STATIC
ProjectMWrapper.cpp
# Add more as implemented
)
target_include_directories(projectm_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
/usr/include/projectM-4
)
target_link_libraries(projectm_lib PUBLIC
Qt6::Core
projectM-4
projectM-4-playlist
OpenGL::GL
)

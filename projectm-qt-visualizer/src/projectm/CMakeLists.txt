add_library(projectm_lib STATIC
ProjectMWrapper.cpp
# Add more as implemented
)
target_include_directories(projectm_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
${PROJECTM4_INCLUDE_DIRS}
)
target_link_libraries(projectm_lib PUBLIC
Qt6::Core
${PROJECTM4_LIBRARIES}
OpenGL::GL
)

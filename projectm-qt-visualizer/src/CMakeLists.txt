# Collect sources from subdirectories
add_subdirectory(core)
add_subdirectory(projectm)
add_subdirectory(audio)
add_subdirectory(gui)
add_subdirectory(platform)
add_subdirectory(app)
# Main executable
add_executable(${PROJECT_NAME}
main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE
app_lib
gui_lib
projectm_lib
audio_lib
core_lib
platform_lib
Qt6::Core
Qt6::Gui
Qt6::Widgets
Qt6::OpenGL
Qt6::OpenGLWidgets
Qt6::Multimedia
projectM-4
projectM-4-playlist
OpenGL::GL
)

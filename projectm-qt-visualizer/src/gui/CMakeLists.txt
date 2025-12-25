add_library(gui_lib STATIC
MainWindow.cpp
VisualizerWidget.cpp
# Add more as implemented
)
target_include_directories(gui_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gui_lib PUBLIC
Qt6::Core
Qt6::Gui
Qt6::Widgets
Qt6::OpenGL
Qt6::OpenGLWidgets
projectm_lib
)

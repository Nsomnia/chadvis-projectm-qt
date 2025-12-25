add_library(app_lib STATIC
# Application.cpp
# Add as implemented
)
target_include_directories(app_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(app_lib PUBLIC
Qt6::Core
core_lib
)
# Currently empty placeholder - uncomment when files exist
# For now, make it header-only placeholder
add_library(app_lib INTERFACE)

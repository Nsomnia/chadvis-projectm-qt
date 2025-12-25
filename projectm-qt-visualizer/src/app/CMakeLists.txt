# Currently empty placeholder
# When implemented, use:
# add_library(app_lib STATIC Application.cpp)
# target_include_directories(app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# target_link_libraries(app_lib PUBLIC Qt6::Core core_lib)
# For now, make it header-only placeholder
add_library(app_lib INTERFACE)
target_include_directories(app_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(app_lib INTERFACE Qt6::Core core_lib)

# Integration tests - require more setup
# Placeholder for now
add_executable(integration_tests
# test_projectm_render.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../unit/test_main.cpp # Reuse placeholder
)
target_link_libraries(integration_tests PRIVATE
Qt6::Core
Qt6::Test
projectm_lib
gui_lib
)
add_test(NAME integration_tests COMMAND integration_tests)

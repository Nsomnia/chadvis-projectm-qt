add_executable(unit_tests
test_main.cpp
core/test_ConfigParsers.cpp
visualizer/test_PresetScanner.cpp
# Add test files as implemented
)
target_link_libraries(unit_tests PRIVATE
Qt6::Core
Qt6::Test
core_lib
audio_lib
projectm_lib
)
add_test(NAME unit_tests COMMAND unit_tests)

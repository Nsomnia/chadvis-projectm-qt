add_executable(unit_tests
    test_main.cpp
    core/test_Logger.cpp
    core/test_ConfigParsers.cpp
    visualizer/test_PresetScanner.cpp
    suno/test_AuthModule.cpp
    suno/test_ClipParser.cpp
)

set_target_properties(unit_tests PROPERTIES
    AUTOMOC ON
)

target_link_libraries(unit_tests PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME unit_tests COMMAND unit_tests)

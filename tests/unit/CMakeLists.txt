add_executable(unit_tests
    test_main.cpp
    core/test_Logger.cpp
    core/test_ConfigParsers.cpp
    core/test_FileUtils.cpp
    visualizer/test_PresetScanner.cpp
    suno/test_AuthModule.cpp
    suno/test_ClipParser.cpp
    suno/test_DownloadQueue.cpp
    suno/test_SunoEndpoints.cpp
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

add_executable(test_ClerkAuthClient
    suno/test_ClerkAuthClient.cpp
)

set_target_properties(test_ClerkAuthClient PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_ClerkAuthClient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_ClerkAuthClient COMMAND test_ClerkAuthClient)

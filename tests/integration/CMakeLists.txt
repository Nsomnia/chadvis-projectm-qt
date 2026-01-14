add_executable(integration_tests
    test_main.cpp
)

set_target_properties(integration_tests PROPERTIES
    AUTOMOC ON
)

target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(integration_tests PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME integration_tests COMMAND integration_tests)

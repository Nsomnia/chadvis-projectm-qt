enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_subdirectory(unit)
add_subdirectory(integration)

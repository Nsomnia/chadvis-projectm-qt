add_library(core_lib STATIC
utils/Logger.cpp
# Add more as implemented
)
target_include_directories(core_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(core_lib PUBLIC
Qt6::Core
)

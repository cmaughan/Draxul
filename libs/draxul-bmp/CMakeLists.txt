add_library(draxul-bmp
    src/bmp.cpp
)

target_include_directories(draxul-bmp PUBLIC include)
target_link_libraries(draxul-bmp
    PUBLIC draxul-types
    PRIVATE draxul-performance
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

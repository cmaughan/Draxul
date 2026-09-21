add_library(draxul-grid
    src/grid.cpp
)
target_include_directories(draxul-grid PUBLIC include)
target_link_libraries(draxul-grid
    PUBLIC draxul-types
    PRIVATE draxul-performance
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

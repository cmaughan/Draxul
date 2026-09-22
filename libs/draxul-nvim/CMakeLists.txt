add_library(draxul-nvim-protocol STATIC
    src/mpack_codec.cpp
    src/ui_events.cpp
    src/input.cpp
)
target_include_directories(draxul-nvim-protocol PUBLIC include)
target_link_libraries(draxul-nvim-protocol
    PUBLIC draxul-types
    PRIVATE
        draxul-performance
        mpack_lib
        SDL3::SDL3
)

add_library(draxul-nvim-transport STATIC
    src/nvim_process.cpp
    src/rpc.cpp
)
target_include_directories(draxul-nvim-transport PUBLIC include)
target_link_libraries(draxul-nvim-transport
    PUBLIC draxul-nvim-protocol
    PRIVATE
        draxul-performance
        draxul-types
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

add_library(draxul-nvim
    src/nvim_process.cpp
    src/mpack_codec.cpp
    src/rpc.cpp
    src/ui_events.cpp
    src/input.cpp
)
target_include_directories(draxul-nvim PUBLIC include)
target_link_libraries(draxul-nvim
    PUBLIC draxul-types mpack_lib
    PRIVATE draxul-performance SDL3::SDL3
)

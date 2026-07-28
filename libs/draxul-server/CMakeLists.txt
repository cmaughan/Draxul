add_library(draxul-server
    src/fake_terminal_runtime.cpp
    src/server_kernel.cpp
)

target_include_directories(draxul-server PUBLIC include)

target_link_libraries(draxul-server PUBLIC
    draxul-control
    draxul-protocol
    draxul-types
)

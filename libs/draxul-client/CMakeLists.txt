add_library(draxul-client
    src/remote_terminal_client.cpp
    src/server_client.cpp
)

target_include_directories(draxul-client PUBLIC include)

target_link_libraries(draxul-client PUBLIC
    draxul-control
    draxul-protocol
)

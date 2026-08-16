add_library(draxul-client
    src/agent_client.cpp
    src/client_recovery.cpp
    src/remote_terminal_client.cpp
    src/remote_session_client.cpp
    src/server_client.cpp
    src/server_control_channel.cpp
    src/topology_client.cpp
    src/topology_projection.cpp
)

target_include_directories(draxul-client PUBLIC include)

target_link_libraries(draxul-client PUBLIC
    draxul-control
    draxul-protocol
    draxul-session-model
)

add_library(draxul-server
    src/fake_terminal_runtime.cpp
    src/remote_terminal_service.cpp
    src/server_agent_service.cpp
    src/session_poll_service.cpp
    src/session_topology_bridge.cpp
    src/server_kernel.cpp
    src/server_kernel_facade.cpp
    src/server_kernel_impl.h
    src/server_terminal_runtime.cpp
    src/topology_service.cpp
)

target_include_directories(draxul-server PUBLIC include)

target_link_libraries(draxul-server PUBLIC
    draxul-agent
    draxul-control
    draxul-protocol
    draxul-session-model
    draxul-terminal-process
    draxul-types
)

add_library(draxul-server
    src/fake_terminal_runtime.cpp
    src/remote_terminal_service.cpp
    src/server_agent_service.cpp
    src/session_poll_service.cpp
    src/session_stream_service.cpp
    src/session_topology_bridge.cpp
    src/server_kernel.cpp
    src/server_kernel_clients.cpp
    src/server_kernel_facade.cpp
    src/server_kernel_impl.h
    src/server_kernel_lifecycle.cpp
    src/server_kernel_requests.cpp
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

# Private server collaborators are deliberately absent from draxul-server's
# public include tree. Focused server tests opt into this boundary explicitly.
add_library(draxul-server-test-internals INTERFACE)
target_include_directories(draxul-server-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-server-test-internals INTERFACE draxul-server)

add_library(draxul-protocol
    src/remote_terminal_protocol.cpp
    src/server_protocol.cpp
    src/topology_protocol.cpp
)

target_include_directories(draxul-protocol PUBLIC include)

target_compile_definitions(draxul-protocol PRIVATE
    DRAXUL_BUILD_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(draxul-protocol PUBLIC
    draxul-agent
    draxul-terminal-core
    nlohmann_json::nlohmann_json
)

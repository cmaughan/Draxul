add_library(draxul-session-model
    src/session_state.cpp
)

target_include_directories(draxul-session-model PUBLIC include)

target_link_libraries(draxul-session-model PUBLIC
    draxul-agent
    draxul-host-identity
)

target_link_libraries(draxul-session-model PRIVATE
    draxul-config
    draxul-performance
    nlohmann_json::nlohmann_json
)

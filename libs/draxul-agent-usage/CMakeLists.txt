# draxul-agent-usage — per-agent token activity from native Codex/Claude
# session files (TokenFu-derived parsing and rate window). Renderer-free and
# server-owned: only sanitized AgentActivity values leave this library.
add_library(draxul-agent-usage STATIC
    src/agent_session_tail.cpp
    src/agent_usage_monitor.cpp
)
target_include_directories(draxul-agent-usage
    PUBLIC include
    PRIVATE src
)
target_link_libraries(draxul-agent-usage
    PUBLIC draxul-agent
    PRIVATE draxul-file-monitor nlohmann_json::nlohmann_json
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

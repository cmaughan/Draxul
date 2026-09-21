add_library(draxul-agent-integration STATIC
    src/agent_integration.cpp
)

target_include_directories(draxul-agent-integration PUBLIC include)
target_link_libraries(draxul-agent-integration
    PRIVATE nlohmann_json::nlohmann_json)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

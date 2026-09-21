add_library(draxul-session-model
    src/session_state.cpp
)

target_include_directories(draxul-session-model PUBLIC include)

target_link_libraries(draxul-session-model PUBLIC
    draxul-agent
    draxul-host-identity
)

target_link_libraries(draxul-session-model PRIVATE
    # Session persistence needs the TOML document/default-path leaf, not the
    # application configuration schema or its SDL-backed input settings.
    draxul-plugin-config-support
    draxul-performance
    nlohmann_json::nlohmann_json
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

add_library(draxul-app-cli STATIC
    src/cli_args.cpp
    src/cli_help.cpp
    src/cli_dispatch.cpp
    src/launch_policy.cpp
    src/control_cli.cpp
    src/integration_cli.cpp
    src/server_status_text.cpp
    src/session_id.cpp
    src/topology_cli.cpp
)
target_include_directories(draxul-app-cli PUBLIC include)
target_link_libraries(draxul-app-cli PUBLIC
    draxul-client draxul-host-api
)
target_link_libraries(draxul-app-cli PRIVATE
    draxul-plugin-config-support draxul-plugin draxul-agent-integration
    nlohmann_json::nlohmann_json
)
if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul-app-cli PRIVATE DRAXUL_ENABLE_RENDER_TESTS)
endif()
draxul_reject_transitive_links(draxul-app-cli draxul-renderer draxul-window)
draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

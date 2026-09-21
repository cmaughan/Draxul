set(DRAXUL_APP_PLATFORM_SOURCES)
if(APPLE)
    list(APPEND DRAXUL_APP_PLATFORM_SOURCES
        macos_menu.mm
        macos_server_status_surface.mm
    )
endif()

add_library(draxul-app
    agent_controller.cpp
    agent_integration.cpp
    app.cpp
    chrome_host.cpp
    chrome_text_layer.cpp
    chrome_vector_pass.cpp
    cli_args.cpp
    cli_help.cpp
    server_status_surface.cpp
    command_palette.cpp
    command_palette_host.cpp
    control_cli.cpp
    control_event_journal.cpp
    control_request_policy.cpp
    control_request_router.cpp
    toast_host.cpp
    diagnostics_panel_host.cpp
    gui_action_handler.cpp
    input_dispatcher.cpp
    pane_manager.cpp
    render_tree.cpp
    session_id.cpp
    space_controller.cpp
    tab_controller.cpp
    topology_cli.cpp
    topology_mutation_route.cpp
    ${DRAXUL_APP_PLATFORM_SOURCES}
)

target_include_directories(draxul-app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(draxul-app PUBLIC
    draxul-agent
    draxul-app-shell
    draxul-session-model
    draxul-control
    draxul-client
    draxul-weather
    nlohmann_json::nlohmann_json
    draxul-config
    draxul-runtime-support
    draxul-render-test
    draxul-renderer
    draxul-ui
    draxul-font
    draxul-grid
    draxul-gui
    draxul-grid-host
    draxul-host-api
    draxul-nvim-host
    draxul-plugin-host
    draxul-terminal-host
    draxul-plugin
    draxul-nanovg
)
target_link_libraries(draxul-app PRIVATE draxul-performance)
target_link_libraries(draxul-app PRIVATE draxul-agent-integration)

# Product implementations are linked only into their native plugin modules;
# the app library does not link a product implementation.
if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul-app PUBLIC DRAXUL_ENABLE_RENDER_TESTS)
endif()

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

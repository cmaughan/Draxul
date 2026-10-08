include(${CMAKE_SOURCE_DIR}/cmake/DraxulTestScopes.cmake)

add_executable(draxul-rpc-fake
    rpc_fake_server.cpp
)
target_link_libraries(draxul-rpc-fake PRIVATE draxul-nvim-protocol)

add_executable(draxul-personal-agent-fake personal_agent_fake.cpp)
target_link_libraries(draxul-personal-agent-fake PRIVATE nlohmann_json::nlohmann_json)

# Header-only fakes and fixtures are shared by every test executable. Keeping
# their include path and Catch dependency in one interface target prevents the
# focused targets below from growing subtly different test environments.
add_library(draxul-test-support INTERFACE)
target_include_directories(draxul-test-support INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
target_link_libraries(draxul-test-support INTERFACE Catch2::Catch2 draxul-types)

# Discover sources for the partition audit; every suite must have an explicit
# owner below. Ownership preserves behavioral coverage, including integration
# cases, while keeping unrelated build dependencies out of focused targets.
file(GLOB DRAXUL_ALL_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *_tests.cpp
)
draxul_get_registered_plugin_test_sources(DRAXUL_PLUGIN_TEST_SOURCES)
set(DRAXUL_APP_TEST_SOURCES
    active_tab_invariant_tests.cpp
    app_config_tests.cpp
    app_dispatch_tests.cpp
    app_pump_tests.cpp
    app_smoke_tests.cpp
    chrome_host_rename_tests.cpp
    chrome_host_tabbar_tests.cpp
    command_palette_tests.cpp
    config_lifecycle_tests.cpp
    control_plane_tests.cpp
    control_request_policy_tests.cpp
    dpi_hotplug_integration_tests.cpp
    frame_timer_tests.cpp
    grid_host_null_handle_tests.cpp
    gui_action_handler_tests.cpp
    pane_manager_tests.cpp
    host_provider_availability_tests.cpp
    input_dispatcher_routing_tests.cpp
    overlay_allocation_failure_tests.cpp
    server_status_surface_tests.cpp
    session_state_tests.cpp
    space_controller_tests.cpp
    startup_rollback_tests.cpp
    tab_keybinding_roundtrip_tests.cpp
    toast_host_tests.cpp
    topology_mutation_route_tests.cpp
)
set(DRAXUL_APP_SHELL_TEST_SOURCES
    app_shell_layout_tests.cpp
    chrome_layout_tests.cpp
    fuzzy_match_tests.cpp
    pane_status_pill_tests.cpp
    rename_editor_tests.cpp
    split_tree_tests.cpp
)
set(DRAXUL_CLI_TEST_SOURCES cli_args_tests.cpp cli_dispatch_tests.cpp)
set(DRAXUL_HOST_API_TEST_SOURCES host_api_tests.cpp)
set(DRAXUL_AGENT_INTEGRATION_TEST_SOURCES agent_integration_tests.cpp)
set(DRAXUL_WEATHER_TEST_SOURCES weather_service_tests.cpp)
set(DRAXUL_NVIM_PROTOCOL_TEST_SOURCES
    clipboard_tests.cpp
    grid_line_bounds_tests.cpp
    input_tests.cpp
    mouse_modifier_tests.cpp
    mpack_fuzz_tests.cpp
    mpackvalue_variant_stability_tests.cpp
    rpc_codec_tests.cpp
    ui_events_tests.cpp
    ui_request_worker_overlap_tests.cpp
)
set(DRAXUL_NVIM_TRANSPORT_TEST_SOURCES
    nvim_crash_tests.cpp
    nvim_partial_init_tests.cpp
    nvim_process_tests.cpp
    result_tests.cpp
    rpc_backpressure_tests.cpp
    rpc_integration_tests.cpp
    shutdown_race_tests.cpp
    unicode_tests.cpp
)
# These parser cases also exercise MarkdownHost and retain its integration links.
set(DRAXUL_MARKDOWN_TEST_SOURCES markdown_parser_tests.cpp)
set(DRAXUL_MARKDOWN_LAYOUT_TEST_SOURCES markdown_layout_tests.cpp)
set(DRAXUL_KANBAN_CORE_TEST_SOURCES
    kanban_board_tests.cpp
    kanban_layout_tests.cpp
    kanban_navigation_tests.cpp
    kanban_store_tests.cpp
)
set(DRAXUL_KANBAN_HOST_TEST_SOURCES kanban_host_tests.cpp)
set(DRAXUL_PERSONAL_HOST_TEST_SOURCES personal_assistant_host_tests.cpp)
set(DRAXUL_FILE_MONITOR_TEST_SOURCES file_monitor_tests.cpp)
set(DRAXUL_NANOVG_PAINT_TEST_SOURCES nanovg_paint_tests.cpp)
set(DRAXUL_PLUGIN_INTEGRATION_TEST_SOURCES plugin_manager_tests.cpp)
set(DRAXUL_PLUGIN_NANOVG_TEST_SOURCES plugin_nanovg_pass_tests.cpp)
set(DRAXUL_RENDER_CONTRACT_TEST_SOURCES
    overlay_unicode_tests.cpp
    render_contract_tests.cpp
    ui_panel_backend_tests.cpp
    ui_panel_layout_tests.cpp
)

set(DRAXUL_CONFIG_TEST_SOURCES
    config_parity_tests.cpp
    config_schema_tests.cpp
    corrupt_config_recovery_tests.cpp
    keybinding_dispatch_tests.cpp
    keybinding_parser_tests.cpp
)

set(DRAXUL_PROTOCOL_TEST_SOURCES
    agent_discovery_tests.cpp
    server_protocol_tests.cpp
    terminal_core_tests.cpp
)

set(DRAXUL_SERVER_TEST_SOURCES
    personal_agent_tests.cpp
    agent_protocol_tests.cpp
    agent_usage_tests.cpp
    topology_projection_tests.cpp
    session_transport_load_tests.cpp
    server_agent_runtime_tests.cpp
    server_client_authentication_tests.cpp
    server_lifecycle_discovery_tests.cpp
    server_service_protocol_tests.cpp
    server_session_checkpoint_tests.cpp
)

set(DRAXUL_SERVER_INTEGRATION_TEST_SOURCES
    server_process_integration_tests.cpp
    server_topology_terminal_tests.cpp
)

set(DRAXUL_CLIENT_TEST_SOURCES
    client_recovery_tests.cpp
    control_transport_tests.cpp
    remote_session_client_tests.cpp
    remote_session_coordinator_tests.cpp
    session_stream_policy_tests.cpp
)

set(DRAXUL_PROCESS_TEST_SOURCES
    agent_process_observer_tests.cpp
    conpty_process_tests.cpp
    fd_inheritance_tests.cpp
    process_util_tests.cpp
    unix_pty_process_tests.cpp
)

set(DRAXUL_FONT_TEST_SOURCES
    box_drawing_tests.cpp
    font_fallback_corpus_tests.cpp
    font_resolver_style_tests.cpp
    font_style_model_tests.cpp
    font_tests.cpp
    glyph_raster_error_tests.cpp
    ligature_atlas_reset_tests.cpp
    rich_text_service_tests.cpp
    text_atlas_builder_tests.cpp
)

set(DRAXUL_RENDERER_TEST_SOURCES
    atlas_overflow_multi_host_tests.cpp
    camera_input_tests.cpp
    dpi_scaling_tests.cpp
    font_size_tests.cpp
    grid_rendering_pipeline_tests.cpp
    ligature_grouping_tests.cpp
    nanovg_demo_host_tests.cpp
    nanovg_vulkan_sync_tests.cpp
    render_comparison_tests.cpp
    render_test_driver_tests.cpp
    render_test_parser_tests.cpp
    renderer_state_tests.cpp
    shader_abi_parity_tests.cpp
    ui_panel_input_tests.cpp
    vk_resource_helpers_tests.cpp
)

set(DRAXUL_MARKDOWN_CORE_TEST_SOURCES
    markdown_draw_list_tests.cpp
    markdown_scroll_tests.cpp
    markdown_navigation_tests.cpp
    markdown_front_matter_tests.cpp
)

set(DRAXUL_CLASSIFIED_TEST_SOURCES
    ${DRAXUL_CLI_TEST_SOURCES}
    ${DRAXUL_PLUGIN_INTEGRATION_TEST_SOURCES}
    ${DRAXUL_CONFIG_TEST_SOURCES}
    ${DRAXUL_PROTOCOL_TEST_SOURCES}
    ${DRAXUL_SERVER_TEST_SOURCES}
    ${DRAXUL_SERVER_INTEGRATION_TEST_SOURCES}
    ${DRAXUL_CLIENT_TEST_SOURCES}
    ${DRAXUL_PROCESS_TEST_SOURCES}
    ${DRAXUL_FONT_TEST_SOURCES}
    ${DRAXUL_RENDERER_TEST_SOURCES}
    ${DRAXUL_MARKDOWN_CORE_TEST_SOURCES}
    ${DRAXUL_APP_TEST_SOURCES}
    ${DRAXUL_APP_SHELL_TEST_SOURCES}
    ${DRAXUL_HOST_API_TEST_SOURCES}
    ${DRAXUL_AGENT_INTEGRATION_TEST_SOURCES}
    ${DRAXUL_WEATHER_TEST_SOURCES}
    ${DRAXUL_NVIM_PROTOCOL_TEST_SOURCES}
    ${DRAXUL_NVIM_TRANSPORT_TEST_SOURCES}
    ${DRAXUL_MARKDOWN_LAYOUT_TEST_SOURCES}
    ${DRAXUL_MARKDOWN_TEST_SOURCES}
    ${DRAXUL_KANBAN_CORE_TEST_SOURCES}
    ${DRAXUL_KANBAN_HOST_TEST_SOURCES}
    ${DRAXUL_PERSONAL_HOST_TEST_SOURCES}
    ${DRAXUL_FILE_MONITOR_TEST_SOURCES}
    ${DRAXUL_NANOVG_PAINT_TEST_SOURCES}
    ${DRAXUL_PLUGIN_NANOVG_TEST_SOURCES}
    ${DRAXUL_RENDER_CONTRACT_TEST_SOURCES}
    ${DRAXUL_PLUGIN_TEST_SOURCES}
)
list(LENGTH DRAXUL_CLASSIFIED_TEST_SOURCES DRAXUL_CLASSIFIED_COUNT)
list(REMOVE_DUPLICATES DRAXUL_CLASSIFIED_TEST_SOURCES)
list(LENGTH DRAXUL_CLASSIFIED_TEST_SOURCES DRAXUL_UNIQUE_CLASSIFIED_COUNT)
if(NOT DRAXUL_CLASSIFIED_COUNT EQUAL DRAXUL_UNIQUE_CLASSIFIED_COUNT)
    message(FATAL_ERROR "A Draxul test source was assigned to more than one focused target")
endif()

set(DRAXUL_CORE_TEST_SOURCES
    attr_id_tests.cpp
    attribute_cache_tests.cpp
    bmp_write_tests.cpp
    celltext_unicode_corpus_tests.cpp
    clipboard_poll_gate_tests.cpp
    cursor_blinker_tests.cpp
    encode_key_tests.cpp
    file_drop_tests.cpp
    grid_host_cursor_tests.cpp
    grid_oob_tests.cpp
    grid_tests.cpp
    highlight_tests.cpp
    hlattr_style_flags_tests.cpp
    log_tests.cpp
    macos_autorelease_pool_tests.cpp
    mouse_reporter_tests.cpp
    pane_print_tests.cpp
    perf_timing_tests.cpp
    remote_terminal_host_tests.cpp
    runtime_path_tests.cpp
    scrollback_overflow_tests.cpp
    scrollback_ring_wrap_tests.cpp
    scrollback_viewport_resize_tests.cpp
    selection_truncation_tests.cpp
    shell_host_crash_tests.cpp
    startup_resize_state_tests.cpp
    terminal_capture_tests.cpp
    terminal_cursor_refresh_tests.cpp
    terminal_mouse_tests.cpp
    terminal_semantic_replay_tests.cpp
    terminal_sgr_tests.cpp
    terminal_surface_parity_tests.cpp
    terminal_vt_psreadline_tests.cpp
    terminal_vt_tests.cpp
    thread_check_tests.cpp
    ui_request_worker_tests.cpp
    vt_parser_fuzz_tests.cpp
    vt_parser_osc_esc_tests.cpp
    vt_parser_partial_sequences_tests.cpp
    vtparser_overflow_tests.cpp
    window_event_registration_tests.cpp
)

# Keep source partitioning verifiable without launching a test process. This
# catches accidental omissions/duplication during target moves, including code
# that is compiled only on the other platform or behind an optional product.
set(DRAXUL_PARTITIONED_TEST_SOURCES
    ${DRAXUL_CORE_TEST_SOURCES}
    ${DRAXUL_CLASSIFIED_TEST_SOURCES}
)
set(DRAXUL_SORTED_ALL_TEST_SOURCES
    ${DRAXUL_ALL_TEST_SOURCES}
    ${DRAXUL_PLUGIN_TEST_SOURCES})
set(DRAXUL_SORTED_PARTITIONED_TEST_SOURCES ${DRAXUL_PARTITIONED_TEST_SOURCES})
list(SORT DRAXUL_SORTED_ALL_TEST_SOURCES)
list(SORT DRAXUL_SORTED_PARTITIONED_TEST_SOURCES)
if(NOT "${DRAXUL_SORTED_ALL_TEST_SOURCES}" STREQUAL
       "${DRAXUL_SORTED_PARTITIONED_TEST_SOURCES}")
    message(FATAL_ERROR
        "Focused Draxul test targets do not partition every *_tests.cpp source exactly once")
endif()

function(draxul_scan_catch_source_metadata prefix)
    set(registration_count 0)
    set(tag_tokens)
    foreach(test_source IN LISTS ARGN)
        if(IS_ABSOLUTE "${test_source}")
            set(test_source_path "${test_source}")
        else()
            set(test_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
        endif()
        file(READ "${test_source_path}" source_text)

        # Every current C++ suite uses Catch2 TEST_CASE. Counting registrations
        # statically also sees platform/option-gated cases that cannot be listed
        # by one host executable.
        string(REGEX MATCHALL
            "(^|[\r\n])[ \t]*TEST_CASE[ \t\r\n]*\\("
            source_registrations
            "${source_text}"
        )
        list(LENGTH source_registrations source_registration_count)
        math(EXPR registration_count
            "${registration_count} + ${source_registration_count}")

        # Restrict tokens to adjacent bracketed string literals, the syntax
        # Catch2 uses for tags. Some fixtures intentionally contain tag-shaped
        # data, so this is a conservative source-preservation fingerprint rather
        # than a replacement for Catch2's runtime list output.
        string(REGEX MATCHALL
            "\"(\\[[A-Za-z_.!][A-Za-z0-9_.!,:~=-]*\\])+\""
            source_tag_strings
            "${source_text}"
        )
        foreach(tag_string IN LISTS source_tag_strings)
            string(REGEX MATCHALL
                "\\[[A-Za-z_.!][A-Za-z0-9_.!,:~=-]*\\]"
                source_tag_tokens
                "${tag_string}"
            )
            list(APPEND tag_tokens ${source_tag_tokens})
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES tag_tokens)
    list(SORT tag_tokens)
    list(LENGTH tag_tokens tag_count)
    set(${prefix}_REGISTRATION_COUNT ${registration_count} PARENT_SCOPE)
    set(${prefix}_TAG_COUNT ${tag_count} PARENT_SCOPE)
    set(${prefix}_TAGS ${tag_tokens} PARENT_SCOPE)
endfunction()

draxul_scan_catch_source_metadata(DRAXUL_ALL
    ${DRAXUL_ALL_TEST_SOURCES}
    ${DRAXUL_PLUGIN_TEST_SOURCES})
draxul_scan_catch_source_metadata(DRAXUL_PARTITIONED ${DRAXUL_PARTITIONED_TEST_SOURCES})
if(NOT DRAXUL_ALL_REGISTRATION_COUNT EQUAL DRAXUL_PARTITIONED_REGISTRATION_COUNT
   OR NOT "${DRAXUL_ALL_TAGS}" STREQUAL "${DRAXUL_PARTITIONED_TAGS}")
    message(FATAL_ERROR
        "Focused Draxul test classification changed the static case/tag inventory")
endif()

# These are the source-level inventories at the modular split boundary. They
# intentionally include every platform and optional product. Additions are
# welcome; a drop requires an explicit baseline update and review.
#
# Slice 7 test-infrastructure consolidation (audit bug #13) rebased all three
# numbers to the then-current inventory: startup_config_tests.cpp merged into
# config_lifecycle_tests.cpp (-1 source; its three near-clone TEST_CASEs were
# kept once, -3 registrations) while terminal_surface_parity_tests.cpp was
# added (+1 source, +1 registration). The ServerKernel responsibility split
# later replaced one monolithic suite with seven focused sources (+6 sources)
# while preserving its 66 registrations and tags.
set(DRAXUL_TEST_SOURCE_BASELINE 213)
set(DRAXUL_TEST_REGISTRATION_BASELINE 2087)
# The Workspace -> Tab terminology migration consolidated the old [workspace]
# tag into the existing [tab] token without removing any test registrations.
set(DRAXUL_TEST_TAG_TOKEN_BASELINE 310)
list(LENGTH DRAXUL_SORTED_ALL_TEST_SOURCES DRAXUL_TEST_SOURCE_COUNT)
# The baseline describes the full inventory with every product mounted. A
# partial checkout (an uninitialized product submodule, or a product switched
# off) legitimately registers fewer sources, so enforcing the full-inventory
# floor there would reject supported configurations. CI always mounts all
# products (DRAXUL_REQUIRE_ENABLED_PLUGINS), so the tripwire keeps full
# strength where deletions must be caught.
if(DRAXUL_HAVE_MEGACITY AND DRAXUL_HAVE_SATVIEW AND DRAXUL_HAVE_SCOREVIEW)
    if(DRAXUL_TEST_SOURCE_COUNT LESS DRAXUL_TEST_SOURCE_BASELINE
       OR DRAXUL_ALL_REGISTRATION_COUNT LESS DRAXUL_TEST_REGISTRATION_BASELINE
       OR DRAXUL_ALL_TAG_COUNT LESS DRAXUL_TEST_TAG_TOKEN_BASELINE)
        message(FATAL_ERROR
            "Draxul static test inventory dropped below the modular-split baseline: "
            "${DRAXUL_TEST_SOURCE_COUNT} sources, "
            "${DRAXUL_ALL_REGISTRATION_COUNT} registrations, "
            "${DRAXUL_ALL_TAG_COUNT} tag tokens")
    endif()
else()
    message(STATUS
        "Draxul static test inventory baseline skipped: not all product "
        "plugins are mounted/enabled")
endif()
message(STATUS
    "Draxul test classification: ${DRAXUL_TEST_SOURCE_COUNT} sources, "
    "${DRAXUL_ALL_REGISTRATION_COUNT} registrations, "
    "${DRAXUL_ALL_TAG_COUNT} tag tokens")

# The historical target name remains a developer-facing aggregate. Scripts can
# continue to build `draxul-tests` while CTest runs the focused executables in
# parallel and reports their module labels.
add_custom_target(draxul-tests)

function(draxul_add_test_target target label shard_count)
    add_executable(${target}
        test_main.cpp
        ${ARGN}
    )
    target_precompile_headers(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/support/test_pch.h
    )
    target_link_libraries(${target} PRIVATE draxul-test-support)
    target_compile_definitions(${target} PRIVATE
        DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        DRAXUL_BUILD_ROOT="${CMAKE_BINARY_DIR}"
        DRAXUL_ENABLE_RENDER_TESTS
    )

    draxul_configure_internal_target(${target})
    add_dependencies(draxul-tests ${target})
    draxul_register_test_target(${target} ${label})

    # Existing mixed suites keep their behavioral cases. Dedicated integration
    # targets say so explicitly instead of labeling every executable unit-only.
    if(NOT "integration" IN_LIST label AND NOT "unit" IN_LIST label)
        list(APPEND label unit)
    endif()
    math(EXPR last_shard "${shard_count} - 1")
    foreach(shard_index RANGE 0 ${last_shard})
        add_test(
            NAME ${target}-shard-${shard_index}
            COMMAND $<TARGET_FILE:${target}>
                --shard-count ${shard_count}
                --shard-index ${shard_index}
        )
        set_tests_properties(${target}-shard-${shard_index}
            PROPERTIES
                LABELS "${label}"
                TIMEOUT 300
        )
    endforeach()
endfunction()

draxul_add_test_target(draxul-test-cli "cli;integration" 1 ${DRAXUL_CLI_TEST_SOURCES})
target_link_libraries(draxul-test-cli PRIVATE draxul-app-cli draxul-server)
draxul_reject_transitive_links(draxul-test-cli draxul-app draxul-renderer draxul-window)

draxul_add_test_target(draxul-test-config "config;unit" 2 ${DRAXUL_CONFIG_TEST_SOURCES})
target_link_libraries(draxul-test-config PRIVATE
    draxul-config
    draxul-performance
    SDL3::Headers)

draxul_add_test_target(draxul-test-protocol "protocol;unit" 2 ${DRAXUL_PROTOCOL_TEST_SOURCES})
target_link_libraries(draxul-test-protocol PRIVATE
    draxul-agent
    draxul-protocol
    draxul-terminal-core)

draxul_add_test_target(draxul-test-server "server;integration" 2 ${DRAXUL_SERVER_TEST_SOURCES})
target_link_libraries(draxul-test-server PRIVATE
    draxul-server-test-internals
    draxul-agent-usage
    draxul-client)

add_dependencies(draxul-test-server draxul-personal-agent-fake)
target_compile_definitions(draxul-test-server PRIVATE
    DRAXUL_PERSONAL_FAKE_PATH="$<TARGET_FILE:draxul-personal-agent-fake>")

draxul_add_test_target(draxul-test-server-integration "server-integration;integration" 2 ${DRAXUL_SERVER_INTEGRATION_TEST_SOURCES})
target_link_libraries(draxul-test-server-integration PRIVATE
    draxul-server-test-internals
    draxul-agent-integration
    draxul-client)

draxul_add_test_target(draxul-test-client "client;integration" 2 ${DRAXUL_CLIENT_TEST_SOURCES})
target_link_libraries(draxul-test-client PRIVATE
    draxul-client
    draxul-control-test-internals)

draxul_add_test_target(draxul-test-process "process;integration" 2 ${DRAXUL_PROCESS_TEST_SOURCES})
target_link_libraries(draxul-test-process PRIVATE
    draxul-terminal-process-test-internals)

draxul_add_test_target(draxul-test-font "font;unit" 2 ${DRAXUL_FONT_TEST_SOURCES})
target_link_libraries(draxul-test-font PRIVATE
    draxul-font-test-internals)

draxul_add_test_target(draxul-test-renderer "renderer;unit" 2 ${DRAXUL_RENDERER_TEST_SOURCES})
target_link_libraries(draxul-test-renderer PRIVATE
    draxul-renderer-test-internals
    draxul-renderer
    draxul-font
    draxul-grid-host
    draxul-nanovg
    draxul-ui
    draxul-render-test
    draxul-runtime-support
    Draxul::PluginSupport::CameraInput)

draxul_add_test_target(draxul-test-markdown-core "markdown-core;unit" 2 ${DRAXUL_MARKDOWN_CORE_TEST_SOURCES})
target_link_libraries(draxul-test-markdown-core PRIVATE
    draxul-markdown
    SDL3::Headers)

draxul_add_test_target(draxul-test-core "core;integration" 4 ${DRAXUL_CORE_TEST_SOURCES})
target_link_libraries(draxul-test-core PRIVATE
    draxul-bmp draxul-performance draxul-grid draxul-config
    draxul-terminal-host draxul-terminal-process draxul-nvim-host
    draxul-server draxul-client draxul-runtime-support draxul-font
    draxul-renderer draxul-window draxul-gui)

# Compile and run all CLI-launching cases in the aggregate, while server-only
# integration tests remain independently buildable without the application.
target_compile_definitions(draxul-test-server-integration PRIVATE
    DRAXUL_EXECUTABLE_PATH="$<TARGET_FILE:draxul>")

draxul_add_test_target(draxul-test-nvim-protocol "nvim;protocol" 2
    ${DRAXUL_NVIM_PROTOCOL_TEST_SOURCES})
target_link_libraries(draxul-test-nvim-protocol PRIVATE
    draxul-grid
    draxul-host-api
    draxul-nvim-protocol
    draxul-runtime-support
)

draxul_add_test_target(draxul-test-nvim-transport "nvim;transport;integration" 2
    ${DRAXUL_NVIM_TRANSPORT_TEST_SOURCES})
target_link_libraries(draxul-test-nvim-transport PRIVATE
    draxul-nvim-transport
    draxul-nvim-protocol
    draxul-runtime-support
    draxul-types
)
add_dependencies(draxul-test-nvim-transport draxul-rpc-fake)
target_compile_definitions(draxul-test-nvim-transport PRIVATE
    DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>")

draxul_add_test_target(draxul-test-app "app;session;integration" 2 ${DRAXUL_APP_TEST_SOURCES})
target_link_libraries(draxul-test-app PRIVATE
    draxul-app
    draxul-markdown-host
    draxul-kanban-host
)
target_compile_definitions(draxul-test-app PRIVATE DRAXUL_TEST_REGISTER_APP_HOSTS)

draxul_add_test_target(draxul-test-app-shell "app-shell" 2
    ${DRAXUL_APP_SHELL_TEST_SOURCES})
target_link_libraries(draxul-test-app-shell PRIVATE draxul-app-shell)

draxul_add_test_target(draxul-test-host-api "host-api" 1
    ${DRAXUL_HOST_API_TEST_SOURCES})
target_link_libraries(draxul-test-host-api PRIVATE draxul-host-api)

draxul_add_test_target(draxul-test-agent-integration "agent-integration" 1
    ${DRAXUL_AGENT_INTEGRATION_TEST_SOURCES})
target_link_libraries(draxul-test-agent-integration PRIVATE
    draxul-agent-integration
    draxul-types
    nlohmann_json::nlohmann_json)

draxul_add_test_target(draxul-test-weather "weather" 2
    ${DRAXUL_WEATHER_TEST_SOURCES})
target_link_libraries(draxul-test-weather PRIVATE
    draxul-weather-test-internals
    draxul-app-shell
    draxul-types)

draxul_add_test_target(draxul-test-markdown-layout "markdown;layout" 1
    ${DRAXUL_MARKDOWN_LAYOUT_TEST_SOURCES})
target_link_libraries(draxul-test-markdown-layout PRIVATE draxul-markdown)

draxul_add_test_target(draxul-test-nanovg-paint "nanovg;paint" 2
    ${DRAXUL_NANOVG_PAINT_TEST_SOURCES})
target_link_libraries(draxul-test-nanovg-paint PRIVATE
    draxul-nanovg-backend draxul-types)

draxul_add_test_target(draxul-test-plugin-nanovg "plugin;nanovg" 2
    ${DRAXUL_PLUGIN_NANOVG_TEST_SOURCES})
target_link_libraries(draxul-test-plugin-nanovg PRIVATE
    Draxul::PluginSupport::NanoVGPass draxul-types)

# These tests deliberately avoid draxul-test-core's broad renderer closure.
# UiPanel's fake implements IFrameContext directly through render support;
# GUI overlays consume their public text contract; and the NanoVG isolation
# executable below forces the platform pass factory to link independently.
draxul_add_test_target(draxul-test-render-contracts "render-contracts" 1
    ${DRAXUL_RENDER_CONTRACT_TEST_SOURCES})
target_link_libraries(draxul-test-render-contracts PRIVATE
    draxul-gui
    draxul-nanovg
    draxul-ui
    draxul-plugin-render-support
)
draxul_reject_transitive_links(draxul-test-render-contracts
    draxul-renderer)

draxul_add_test_target(draxul-test-plugin-integration "plugin;integration" 2
    ${DRAXUL_PLUGIN_INTEGRATION_TEST_SOURCES})
target_link_libraries(draxul-test-plugin-integration PRIVATE
    draxul-plugin draxul-plugin-host draxul-font draxul-renderer
    nlohmann_json::nlohmann_json)
if(APPLE)
    # Native modules resolve their SDL calls from the hosting executable.
    # A focused loader test must supply the complete SDL ABI, including APIs
    # not directly called by the test (audio, input, and product backends).
    target_link_options(draxul-test-plugin-integration PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:SDL3::SDL3>")
endif()
target_include_directories(draxul-test-plugin-integration PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/draxul-host/src")

add_library(draxul-plugin-fixture MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
set_target_properties(draxul-plugin-fixture PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-replacement MODULE
    fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture-replacement PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
target_compile_definitions(draxul-plugin-fixture-replacement PRIVATE
    DRAXUL_FIXTURE_PLUGIN_VERSION="2.0.0")
set_target_properties(draxul-plugin-fixture-replacement PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-bad-abi MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture-bad-abi PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
target_compile_definitions(draxul-plugin-fixture-bad-abi PRIVATE
    DRAXUL_FIXTURE_REJECT_ABI)
set_target_properties(draxul-plugin-fixture-bad-abi PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-wrong-id MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture-wrong-id PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
target_compile_definitions(draxul-plugin-fixture-wrong-id PRIVATE
    DRAXUL_FIXTURE_PLUGIN_ID="dev.draxul.wrong")
set_target_properties(draxul-plugin-fixture-wrong-id PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-unsupported MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture-unsupported PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
target_compile_definitions(draxul-plugin-fixture-unsupported PRIVATE
    DRAXUL_FIXTURE_BACKENDS=0)
set_target_properties(draxul-plugin-fixture-unsupported PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-fail-create MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture-fail-create PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
target_compile_definitions(draxul-plugin-fixture-fail-create PRIVATE
    DRAXUL_FIXTURE_FAIL_CREATE)
set_target_properties(draxul-plugin-fixture-fail-create PROPERTIES PREFIX "")

add_library(draxul-plugin-fixture-no-symbol MODULE
    fixtures/plugin_fixture_no_symbol.cpp)
set_target_properties(draxul-plugin-fixture-no-symbol PROPERTIES PREFIX "")

add_dependencies(draxul-test-plugin-integration
    draxul-spinning-triangle
    draxul-plugin-fixture
    draxul-plugin-fixture-replacement
    draxul-plugin-fixture-bad-abi
    draxul-plugin-fixture-wrong-id
    draxul-plugin-fixture-unsupported
    draxul-plugin-fixture-fail-create
    draxul-plugin-fixture-no-symbol)
target_compile_definitions(draxul-test-plugin-integration PRIVATE
    DRAXUL_TRIANGLE_PLUGIN_PATH="$<TARGET_FILE:draxul-spinning-triangle>"
    DRAXUL_FIXTURE_VALID_PATH="$<TARGET_FILE:draxul-plugin-fixture>"
    DRAXUL_FIXTURE_REPLACEMENT_PATH="$<TARGET_FILE:draxul-plugin-fixture-replacement>"
    DRAXUL_FIXTURE_BAD_ABI_PATH="$<TARGET_FILE:draxul-plugin-fixture-bad-abi>"
    DRAXUL_FIXTURE_WRONG_ID_PATH="$<TARGET_FILE:draxul-plugin-fixture-wrong-id>"
    DRAXUL_FIXTURE_UNSUPPORTED_PATH="$<TARGET_FILE:draxul-plugin-fixture-unsupported>"
    DRAXUL_FIXTURE_FAIL_CREATE_PATH="$<TARGET_FILE:draxul-plugin-fixture-fail-create>"
    DRAXUL_FIXTURE_NO_SYMBOL_PATH="$<TARGET_FILE:draxul-plugin-fixture-no-symbol>"
)
if(TARGET draxul-satview-plugin)
    if(APPLE)
        target_compile_definitions(draxul-test-plugin-integration PRIVATE
            DRAXUL_TEST_SATVIEW_PACKAGE_ROOT="$<TARGET_BUNDLE_DIR:draxul>/Contents/PlugIns")
    else()
        target_compile_definitions(draxul-test-plugin-integration PRIVATE
            DRAXUL_TEST_SATVIEW_PACKAGE_ROOT="${CMAKE_BINARY_DIR}/plugins")
    endif()
endif()
draxul_add_test_target(
    draxul-test-markdown-kanban "markdown" 1
    ${DRAXUL_MARKDOWN_TEST_SOURCES}
)
target_link_libraries(draxul-test-markdown-kanban PRIVATE
    draxul-config
    draxul-markdown
    draxul-markdown-host
)

draxul_add_test_target(draxul-test-kanban-core "kanban" 1
    ${DRAXUL_KANBAN_CORE_TEST_SOURCES})
target_link_libraries(draxul-test-kanban-core PRIVATE
    draxul-kanban
    SDL3::Headers
)

draxul_add_test_target(draxul-test-file-monitor "file-monitor" 1
    ${DRAXUL_FILE_MONITOR_TEST_SOURCES})
target_link_libraries(draxul-test-file-monitor PRIVATE
    draxul-file-monitor
    draxul-types
)

draxul_add_test_target(draxul-test-kanban-host "kanban;host;integration" 1
    ${DRAXUL_KANBAN_HOST_TEST_SOURCES})
target_link_libraries(draxul-test-kanban-host PRIVATE
    draxul-kanban-host
    draxul-font
    draxul-imgui-core
    draxul-renderer
    draxul-window
    SDL3::SDL3
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME draxul-plugin-target-suffix
    COMMAND ${CMAKE_COMMAND}
        -S "${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin_target_suffix"
        -B "${CMAKE_CURRENT_BINARY_DIR}/plugin_target_suffix")

draxul_configure_registered_plugin_tests()

set(DRAXUL_EXTERNAL_SDK_SMOKE_COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/external_plugin_sdk_smoke.py
    --cmake ${CMAKE_COMMAND}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --draxul $<TARGET_FILE:draxul>
    --config $<CONFIG>
    --generator ${CMAKE_GENERATOR}
    --platform=${CMAKE_GENERATOR_PLATFORM}
    --toolset=${CMAKE_GENERATOR_TOOLSET}
    --c-compiler=${CMAKE_C_COMPILER}
    --cxx-compiler=${CMAKE_CXX_COMPILER}
    --make-program=${CMAKE_MAKE_PROGRAM}
    --toolchain-file=${CMAKE_TOOLCHAIN_FILE})
option(DRAXUL_ENABLE_EXTERNAL_SDK_RENDER_SMOKE
    "Render the external SDK fixture after build and discovery"
    ${DRAXUL_ENABLE_RENDER_TESTS})
if(DRAXUL_ENABLE_RENDER_TESTS
    AND DRAXUL_ENABLE_EXTERNAL_SDK_RENDER_SMOKE)
    list(APPEND DRAXUL_EXTERNAL_SDK_SMOKE_COMMAND --render)
endif()

add_test(
    NAME draxul-external-plugin-sdk-smoke
    COMMAND ${DRAXUL_EXTERNAL_SDK_SMOKE_COMMAND})
set_tests_properties(draxul-external-plugin-sdk-smoke
    PROPERTIES
        LABELS "integration;plugin;sdk"
        RESOURCE_LOCK draxul_gpu
        TIMEOUT 600)

add_custom_target(draxul-sdk-external-smoke
    COMMAND ${DRAXUL_EXTERNAL_SDK_SMOKE_COMMAND}
    DEPENDS draxul
    USES_TERMINAL
    VERBATIM)

add_test(
    NAME draxul-do-py-tests
    COMMAND ${Python3_EXECUTABLE} -m unittest tests.do_py_tests
)
set_tests_properties(draxul-do-py-tests
    PROPERTIES
        LABELS "unit;python"
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME draxul-review-skill-py-tests
    COMMAND ${Python3_EXECUTABLE} -m unittest tests.review_skill_tests
)
set_tests_properties(draxul-review-skill-py-tests
    PROPERTIES
        LABELS "unit;python"
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

draxul_configure_internal_target(draxul-rpc-fake)
draxul_configure_internal_target(draxul-personal-agent-fake)

# These are build-only contracts, not runtime tests. Each translation unit
# consumes a public API through only its owning target so missing direct
# include/link declarations cannot be masked by the broad unit-test graph.
add_executable(draxul-bmp-link-isolation link_isolation/bmp_public_header.cpp)
target_link_libraries(draxul-bmp-link-isolation PRIVATE draxul-bmp)

add_executable(draxul-plugin-sdk-c-link-isolation
    link_isolation/plugin_sdk_c_header.c)
target_link_libraries(draxul-plugin-sdk-c-link-isolation PRIVATE
    Draxul::PluginSDK)

add_executable(draxul-performance-link-isolation link_isolation/performance_public_header.cpp)
target_link_libraries(draxul-performance-link-isolation PRIVATE draxul-performance)

add_executable(draxul-types-link-isolation link_isolation/types_public_header.cpp)
target_link_libraries(draxul-types-link-isolation PRIVATE draxul-types)

add_executable(draxul-font-link-isolation link_isolation/font_public_headers.cpp)
target_link_libraries(draxul-font-link-isolation PRIVATE draxul-font)

add_executable(draxul-nvim-protocol-link-isolation
    link_isolation/nvim_protocol_public_headers.cpp)
target_link_libraries(draxul-nvim-protocol-link-isolation PRIVATE
    draxul-nvim-protocol)

add_executable(draxul-nvim-transport-link-isolation
    link_isolation/nvim_transport_public_headers.cpp)
target_link_libraries(draxul-nvim-transport-link-isolation PRIVATE
    draxul-nvim-transport)

add_executable(draxul-host-api-link-isolation
    link_isolation/host_api_public_headers.cpp)
target_link_libraries(draxul-host-api-link-isolation PRIVATE draxul-host-api)

add_executable(draxul-app-shell-link-isolation
    link_isolation/app_shell_public_headers.cpp)
target_link_libraries(draxul-app-shell-link-isolation PRIVATE draxul-app-shell)

add_executable(draxul-kanban-core-link-isolation
    link_isolation/kanban_core_public_headers.cpp)
target_link_libraries(draxul-kanban-core-link-isolation PRIVATE draxul-kanban)

add_executable(draxul-weather-link-isolation
    link_isolation/weather_public_header.cpp)
target_link_libraries(draxul-weather-link-isolation PRIVATE draxul-weather)

add_executable(draxul-agent-integration-link-isolation
    link_isolation/agent_integration_public_header.cpp)
target_link_libraries(draxul-agent-integration-link-isolation PRIVATE
    draxul-agent-integration)

add_executable(draxul-terminal-core-link-isolation link_isolation/terminal_core_public_header.cpp)
target_link_libraries(draxul-terminal-core-link-isolation PRIVATE draxul-terminal-core)

add_executable(draxul-terminal-process-link-isolation link_isolation/terminal_process_public_header.cpp)
target_link_libraries(draxul-terminal-process-link-isolation PRIVATE draxul-terminal-process)

add_executable(draxul-protocol-link-isolation link_isolation/protocol_public_header.cpp)
target_link_libraries(draxul-protocol-link-isolation PRIVATE draxul-protocol)

add_executable(draxul-client-link-isolation link_isolation/client_public_header.cpp)
target_link_libraries(draxul-client-link-isolation PRIVATE draxul-client)

add_executable(draxul-server-link-isolation link_isolation/server_public_header.cpp)
target_link_libraries(draxul-server-link-isolation PRIVATE draxul-server)

add_executable(draxul-gui-render-contract-isolation
    link_isolation/gui_render_contract.cpp)
target_link_libraries(draxul-gui-render-contract-isolation PRIVATE draxul-gui)

add_executable(draxul-ui-render-contract-isolation
    link_isolation/ui_render_contract.cpp)
target_link_libraries(draxul-ui-render-contract-isolation PRIVATE draxul-ui)

add_executable(draxul-nanovg-render-contract-isolation
    link_isolation/nanovg_render_contract.cpp)
target_link_libraries(draxul-nanovg-render-contract-isolation PRIVATE
    draxul-nanovg)

foreach(_render_contract_consumer
    draxul-gui-render-contract-isolation
    draxul-ui-render-contract-isolation
    draxul-nanovg-render-contract-isolation)
    draxul_reject_transitive_links(${_render_contract_consumer}
        draxul-renderer)
endforeach()

add_dependencies(draxul-test-render-contracts
    draxul-gui-render-contract-isolation
    draxul-ui-render-contract-isolation
    draxul-nanovg-render-contract-isolation)

add_dependencies(draxul-tests
    draxul-bmp-link-isolation
    draxul-plugin-sdk-c-link-isolation
    draxul-performance-link-isolation
    draxul-types-link-isolation
    draxul-font-link-isolation
    draxul-nvim-protocol-link-isolation
    draxul-nvim-transport-link-isolation
    draxul-host-api-link-isolation
    draxul-app-shell-link-isolation
    draxul-kanban-core-link-isolation
    draxul-weather-link-isolation
    draxul-agent-integration-link-isolation
    draxul-terminal-core-link-isolation
    draxul-terminal-process-link-isolation
    draxul-protocol-link-isolation
    draxul-client-link-isolation
    draxul-server-link-isolation
    draxul-gui-render-contract-isolation
    draxul-ui-render-contract-isolation
    draxul-nanovg-render-contract-isolation
)

# Build-only public-header contracts accompany the core behavior suites.
add_dependencies(draxul-tests-core
    draxul-bmp-link-isolation
    draxul-plugin-sdk-c-link-isolation
    draxul-performance-link-isolation
    draxul-types-link-isolation
    draxul-font-link-isolation
    draxul-nvim-protocol-link-isolation
    draxul-nvim-transport-link-isolation
    draxul-host-api-link-isolation
    draxul-app-shell-link-isolation
    draxul-kanban-core-link-isolation
    draxul-weather-link-isolation
    draxul-agent-integration-link-isolation
    draxul-terminal-core-link-isolation
    draxul-terminal-process-link-isolation
    draxul-protocol-link-isolation
    draxul-client-link-isolation
    draxul-server-link-isolation
    draxul-gui-render-contract-isolation
    draxul-ui-render-contract-isolation
    draxul-nanovg-render-contract-isolation
)

add_test(NAME draxul-dependency-boundary-positive
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_boundary
        -B ${CMAKE_CURRENT_BINARY_DIR}/dependency-boundary-positive
        -DDRAXUL_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DCASE=positive)
set_tests_properties(draxul-dependency-boundary-positive PROPERTIES
    LABELS "unit;cmake" TIMEOUT 60)

add_test(NAME draxul-dependency-boundary-negative
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_boundary
        -B ${CMAKE_CURRENT_BINARY_DIR}/dependency-boundary-negative
        -DDRAXUL_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DCASE=negative)
set_tests_properties(draxul-dependency-boundary-negative PROPERTIES
    LABELS "unit;cmake" TIMEOUT 60 WILL_FAIL TRUE)

add_test(NAME draxul-internal-target-policy-positive
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/internal_target_policy
        -B ${CMAKE_CURRENT_BINARY_DIR}/internal-target-policy-positive
        -DDRAXUL_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DCASE=positive)
set_tests_properties(draxul-internal-target-policy-positive PROPERTIES
    LABELS "unit;cmake" TIMEOUT 60)

add_test(NAME draxul-internal-target-policy-negative
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/internal_target_policy
        -B ${CMAKE_CURRENT_BINARY_DIR}/internal-target-policy-negative
        -DDRAXUL_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DCASE=negative)
set_tests_properties(draxul-internal-target-policy-negative PROPERTIES
    LABELS "unit;cmake" TIMEOUT 60 WILL_FAIL TRUE)

add_test(NAME draxul-test-scope-integration
    COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_scope_integration)
set_tests_properties(draxul-test-scope-integration PROPERTIES
    LABELS "integration;cmake;python" TIMEOUT 120 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

draxul_add_test_target(draxul-test-personal-host "personal;host;integration" 1
    ${DRAXUL_PERSONAL_HOST_TEST_SOURCES})
target_link_libraries(draxul-test-personal-host PRIVATE
    draxul-personal-assistant-host draxul-font draxul-imgui-core
    draxul-renderer draxul-window SDL3::SDL3)

draxul_register_test_scopes()

# Headless ownership is a configure-time dependency contract, not just a name.
foreach(target draxul-test-config draxul-test-protocol draxul-test-server
        draxul-test-client draxul-test-process draxul-test-font draxul-test-markdown-core)
    draxul_reject_transitive_links(${target} draxul-app draxul-renderer draxul-window)
endforeach()

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(draxul-rpc-fake
    rpc_fake_server.cpp
)
target_link_libraries(draxul-rpc-fake PRIVATE draxul-nvim)

add_executable(draxul-tests
    test_main.cpp
    mouse_modifier_tests.cpp
    app_config_tests.cpp
    cursor_blinker_tests.cpp
    log_tests.cpp
    highlight_tests.cpp
    font_tests.cpp
    grid_tests.cpp
    grid_line_bounds_tests.cpp
    grid_rendering_pipeline_tests.cpp
    ui_events_tests.cpp
    input_tests.cpp
    unicode_tests.cpp
    renderer_state_tests.cpp
    ui_request_worker_tests.cpp
    startup_resize_state_tests.cpp
    vk_resource_helpers_tests.cpp
    rpc_codec_tests.cpp
    rpc_integration_tests.cpp
    nvim_process_tests.cpp
    render_test_parser_tests.cpp
    ui_panel_layout_tests.cpp
    clipboard_tests.cpp
    font_size_tests.cpp
    mpack_fuzz_tests.cpp
    nvim_crash_tests.cpp
    dpi_scaling_tests.cpp
    shutdown_race_tests.cpp
    font_fallback_corpus_tests.cpp
    terminal_vt_tests.cpp
    mpackvalue_variant_stability_tests.cpp
    terminal_mouse_tests.cpp
    selection_truncation_tests.cpp
    scrollback_overflow_tests.cpp
    ligature_atlas_reset_tests.cpp
    rpc_backpressure_tests.cpp
    keybinding_dispatch_tests.cpp
    hlattr_style_flags_tests.cpp
    resize_cascade_tests.cpp
    startup_rollback_tests.cpp
    startup_config_tests.cpp
    config_lifecycle_tests.cpp
    powershell_host_tests.cpp
    encode_key_tests.cpp
    attr_id_tests.cpp
    gui_action_handler_tests.cpp
    ui_panel_input_tests.cpp
    input_dispatcher_routing_tests.cpp
    host_manager_tests.cpp
    shell_host_crash_tests.cpp
    scrollback_viewport_resize_tests.cpp
    vt_parser_fuzz_tests.cpp
    vtparser_overflow_tests.cpp
    renderer_shutdown_tests.cpp
    keybinding_conflict_tests.cpp
    file_drop_tests.cpp
    split_tree_tests.cpp
    frame_timer_tests.cpp
    # GuiActionHandler, InputDispatcher, App, and HostManager live in app/ but
    # can be compiled directly into the test binary via the DI seams in AppOptions.
    ${CMAKE_SOURCE_DIR}/app/gui_action_handler.cpp
    ${CMAKE_SOURCE_DIR}/app/input_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/app/app.cpp
    ${CMAKE_SOURCE_DIR}/app/host_manager.cpp
    ${CMAKE_SOURCE_DIR}/app/split_tree.cpp
    $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/app/macos_menu.mm>
)

target_include_directories(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/libs/draxul-window/src
)
target_link_libraries(draxul-tests PRIVATE draxul-app-support draxul-font draxul-grid draxul-host draxul-nvim draxul-renderer draxul-ui draxul-window Catch2::Catch2)
target_compile_definitions(draxul-tests PRIVATE
    DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>"
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    DRAXUL_ENABLE_RENDER_TESTS
)

add_test(NAME draxul-tests COMMAND $<TARGET_FILE:draxul-tests>)

draxul_apply_sanitizers(draxul-tests)
draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-tests)
draxul_apply_coverage(draxul-rpc-fake)

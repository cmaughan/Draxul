add_executable(draxul-rpc-fake
    rpc_fake_server.cpp
)
target_link_libraries(draxul-rpc-fake PRIVATE draxul-nvim)

# Header-only fakes and fixtures are shared by every test executable. Keeping
# their include path and Catch dependency in one interface target prevents the
# focused targets below from growing subtly different test environments.
add_library(draxul-test-support INTERFACE)
target_include_directories(draxul-test-support INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
target_link_libraries(draxul-test-support INTERFACE Catch2::Catch2)

# New *_tests.cpp files are picked up automatically, then assigned to a stable
# product boundary. Unclassified files intentionally land in core: a new test
# that needs an app or optional-module dependency will fail to link until it is
# explicitly classified here.
file(GLOB DRAXUL_ALL_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *_tests.cpp
)

set(DRAXUL_APP_TEST_SOURCES
    active_tab_invariant_tests.cpp
    app_config_tests.cpp
    app_shell_layout_tests.cpp
    app_dispatch_tests.cpp
    app_pump_tests.cpp
    app_smoke_tests.cpp
    chrome_layout_tests.cpp
    chrome_host_rename_tests.cpp
    chrome_host_tabbar_tests.cpp
    cli_args_tests.cpp
    command_palette_tests.cpp
    config_lifecycle_tests.cpp
    control_plane_tests.cpp
    dpi_hotplug_integration_tests.cpp
    frame_timer_tests.cpp
    fuzzy_match_tests.cpp
    grid_host_null_handle_tests.cpp
    gui_action_handler_tests.cpp
    pane_manager_tests.cpp
    host_provider_availability_tests.cpp
    http_weather_tests.cpp
    input_dispatcher_routing_tests.cpp
    overlay_allocation_failure_tests.cpp
    pane_status_pill_tests.cpp
    session_cli_tests.cpp
    session_listing_tests.cpp
    session_state_tests.cpp
    space_controller_tests.cpp
    split_tree_tests.cpp
    startup_config_tests.cpp
    startup_rollback_tests.cpp
    tab_keybinding_roundtrip_tests.cpp
    toast_host_tests.cpp
)
file(GLOB DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    markdown_*_tests.cpp
    kanban_*_tests.cpp
)

file(GLOB DRAXUL_MEGACITY_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    city_presentation_*_tests.cpp
    draxul_geometry_tests.cpp
    lcov_coverage_tests.cpp
    megacity_*_tests.cpp
    treesitter*_tests.cpp
)

file(GLOB DRAXUL_SATVIEW_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    satview_*_tests.cpp
)

file(GLOB DRAXUL_SCOREVIEW_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    notation_*_tests.cpp
    scoreview_*_tests.cpp
)
set(DRAXUL_SCOREVIEW_HOST_TEST_SOURCES
    scoreview_composer_tests.cpp
    scoreview_host_orchestration_tests.cpp
    scoreview_host_rebuild_tests.cpp
    scoreview_microphone_tests.cpp
    scoreview_overlay_tests.cpp
    scoreview_worker_stress_tests.cpp
)
list(REMOVE_ITEM DRAXUL_SCOREVIEW_TEST_SOURCES ${DRAXUL_SCOREVIEW_HOST_TEST_SOURCES})

set(DRAXUL_CLASSIFIED_TEST_SOURCES
    ${DRAXUL_APP_TEST_SOURCES}
    ${DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES}
    ${DRAXUL_MEGACITY_TEST_SOURCES}
    ${DRAXUL_SATVIEW_TEST_SOURCES}
    ${DRAXUL_SCOREVIEW_TEST_SOURCES}
    ${DRAXUL_SCOREVIEW_HOST_TEST_SOURCES}
)
list(LENGTH DRAXUL_CLASSIFIED_TEST_SOURCES DRAXUL_CLASSIFIED_COUNT)
list(REMOVE_DUPLICATES DRAXUL_CLASSIFIED_TEST_SOURCES)
list(LENGTH DRAXUL_CLASSIFIED_TEST_SOURCES DRAXUL_UNIQUE_CLASSIFIED_COUNT)
if(NOT DRAXUL_CLASSIFIED_COUNT EQUAL DRAXUL_UNIQUE_CLASSIFIED_COUNT)
    message(FATAL_ERROR "A Draxul test source was assigned to more than one focused target")
endif()

set(DRAXUL_CORE_TEST_SOURCES ${DRAXUL_ALL_TEST_SOURCES})
list(REMOVE_ITEM DRAXUL_CORE_TEST_SOURCES ${DRAXUL_CLASSIFIED_TEST_SOURCES})

# Keep source partitioning verifiable without launching a test process. This
# catches accidental omissions/duplication during target moves, including code
# that is compiled only on the other platform or behind an optional product.
set(DRAXUL_PARTITIONED_TEST_SOURCES
    ${DRAXUL_CORE_TEST_SOURCES}
    ${DRAXUL_CLASSIFIED_TEST_SOURCES}
)
set(DRAXUL_SORTED_ALL_TEST_SOURCES ${DRAXUL_ALL_TEST_SOURCES})
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
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}" source_text)

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

draxul_scan_catch_source_metadata(DRAXUL_ALL ${DRAXUL_ALL_TEST_SOURCES})
draxul_scan_catch_source_metadata(DRAXUL_PARTITIONED ${DRAXUL_PARTITIONED_TEST_SOURCES})
if(NOT DRAXUL_ALL_REGISTRATION_COUNT EQUAL DRAXUL_PARTITIONED_REGISTRATION_COUNT
   OR NOT "${DRAXUL_ALL_TAGS}" STREQUAL "${DRAXUL_PARTITIONED_TAGS}")
    message(FATAL_ERROR
        "Focused Draxul test classification changed the static case/tag inventory")
endif()

# These are the source-level inventories at the modular split boundary. They
# intentionally include every platform and optional product. Additions are
# welcome; a drop requires an explicit baseline update and review.
set(DRAXUL_TEST_SOURCE_BASELINE 190)
set(DRAXUL_TEST_REGISTRATION_BASELINE 1815)
# The Workspace -> Tab terminology migration consolidated the old [workspace]
# tag into the existing [tab] token without removing any test registrations.
set(DRAXUL_TEST_TAG_TOKEN_BASELINE 228)
list(LENGTH DRAXUL_ALL_TEST_SOURCES DRAXUL_TEST_SOURCE_COUNT)
if(DRAXUL_TEST_SOURCE_COUNT LESS DRAXUL_TEST_SOURCE_BASELINE
   OR DRAXUL_ALL_REGISTRATION_COUNT LESS DRAXUL_TEST_REGISTRATION_BASELINE
   OR DRAXUL_ALL_TAG_COUNT LESS DRAXUL_TEST_TAG_TOKEN_BASELINE)
    message(FATAL_ERROR
        "Draxul static test inventory dropped below the modular-split baseline: "
        "${DRAXUL_TEST_SOURCE_COUNT} sources, "
        "${DRAXUL_ALL_REGISTRATION_COUNT} registrations, "
        "${DRAXUL_ALL_TAG_COUNT} tag tokens")
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
        DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>"
        DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        DRAXUL_ENABLE_RENDER_TESTS
    )

    draxul_apply_sanitizers(${target})
    draxul_apply_coverage(${target})
    draxul_apply_msvc_parallel_pdb_fix(${target})
    add_dependencies(draxul-tests ${target})

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
                LABELS "unit;${label}"
                TIMEOUT 300
        )
    endforeach()
endfunction()

draxul_add_test_target(draxul-test-core core 4 ${DRAXUL_CORE_TEST_SOURCES})
target_link_libraries(draxul-test-core PRIVATE
    draxul-bmp
    draxul-performance
    draxul-app-support
    draxul-http
    draxul-window
    draxul-renderer
    draxul-ui
    draxul-font
    draxul-grid
    draxul-nvim
    draxul-config
    draxul-client
    draxul-protocol
    draxul-server
    draxul-gui
    draxul-runtime-support
    draxul-render-test
    draxul-host
    draxul-nanovg
    draxul-renderer-test-internals
)
# Four core suites launch this helper through DRAXUL_RPC_FAKE_PATH. Building a
# focused core target must therefore remain sufficient on its own.
add_dependencies(draxul-test-core draxul-rpc-fake)

draxul_add_test_target(draxul-test-app "app;session" 2 ${DRAXUL_APP_TEST_SOURCES})
target_link_libraries(draxul-test-app PRIVATE
    draxul-app
    draxul-markdown-host
    draxul-kanban
)
target_compile_definitions(draxul-test-app PRIVATE DRAXUL_TEST_REGISTER_APP_HOSTS)
if(DRAXUL_ENABLE_MEGACITY)
    target_link_libraries(draxul-test-app PRIVATE draxul-megacity)
    target_compile_definitions(draxul-test-app PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()
if(DRAXUL_ENABLE_SATVIEW)
    target_link_libraries(draxul-test-app PRIVATE draxul-satview-host)
    target_compile_definitions(draxul-test-app PRIVATE DRAXUL_ENABLE_SATVIEW)
endif()
if(DRAXUL_ENABLE_SCOREVIEW)
    target_link_libraries(draxul-test-app PRIVATE draxul-scoreview-host)
    target_compile_definitions(draxul-test-app PRIVATE DRAXUL_ENABLE_SCOREVIEW)
endif()

draxul_add_test_target(
    draxul-test-markdown-kanban "markdown;kanban" 1
    ${DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES}
)
target_link_libraries(draxul-test-markdown-kanban PRIVATE
    draxul-markdown
    draxul-markdown-host
    draxul-kanban
)

if(DRAXUL_ENABLE_MEGACITY)
    draxul_add_test_target(draxul-test-megacity megacity 2 ${DRAXUL_MEGACITY_TEST_SOURCES})
    target_link_libraries(draxul-test-megacity PRIVATE
        draxul-geometry
        draxul-megacity-test-internals
        draxul-codeviz-renderer-test-internals
    )
    target_compile_definitions(draxul-test-megacity PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()

if(DRAXUL_ENABLE_SATVIEW)
    draxul_add_test_target(draxul-test-satview satview 2 ${DRAXUL_SATVIEW_TEST_SOURCES})
    target_link_libraries(draxul-test-satview PRIVATE
        draxul-satview-host-test-internals
        draxul-satview-renderer-test-internals
    )
    target_compile_definitions(draxul-test-satview PRIVATE DRAXUL_ENABLE_SATVIEW)
endif()

if(DRAXUL_ENABLE_SCOREVIEW)
    draxul_add_test_target(draxul-test-scoreview scoreview 2 ${DRAXUL_SCOREVIEW_TEST_SOURCES})
    target_link_libraries(draxul-test-scoreview PRIVATE draxul-notation draxul-scoreview)

    # Host lifecycle, microphone, NanoVG overlay, and worker orchestration tests
    # carry the full host dependency without imposing it on the ScoreView core.
    draxul_add_test_target(
        draxul-test-scoreview-host "scoreview;scoreview-host" 1
        ${DRAXUL_SCOREVIEW_HOST_TEST_SOURCES}
    )
    target_link_libraries(draxul-test-scoreview-host PRIVATE
        draxul-scoreview-host-test-internals
    )

    foreach(scoreview_target draxul-test-scoreview draxul-test-scoreview-host)
        target_compile_definitions(${scoreview_target} PRIVATE
            DRAXUL_ENABLE_SCOREVIEW
            DRAXUL_VEROVIO_DATA_DIR="${verovio_SOURCE_DIR}/data"
            DRAXUL_SOUNDFONT_DIR="${ydp_grand_piano_SOURCE_DIR}"
        )
        if(WIN32)
            add_custom_command(TARGET ${scoreview_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:verovio>
                    $<TARGET_FILE_DIR:${scoreview_target}>
            )
        endif()
    endforeach()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
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

if(DRAXUL_ENABLE_SATVIEW)
    add_test(
        NAME draxul-satview-catalog-py-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.satview_catalog_py_tests
    )
    set_tests_properties(draxul-satview-catalog-py-tests
        PROPERTIES
            LABELS "unit;satview;python"
            TIMEOUT 120
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-rpc-fake)
draxul_apply_msvc_parallel_pdb_fix(draxul-rpc-fake)

# These are build-only contracts, not runtime tests. Each translation unit
# consumes a public API through only its owning target so missing direct
# include/link declarations cannot be masked by the broad unit-test graph.
add_executable(draxul-bmp-link-isolation link_isolation/bmp_public_header.cpp)
target_link_libraries(draxul-bmp-link-isolation PRIVATE draxul-bmp)

add_executable(draxul-performance-link-isolation link_isolation/performance_public_header.cpp)
target_link_libraries(draxul-performance-link-isolation PRIVATE draxul-performance)

add_executable(draxul-host-link-isolation link_isolation/host_public_header.cpp)
target_link_libraries(draxul-host-link-isolation PRIVATE draxul-host)

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

add_dependencies(draxul-tests
    draxul-bmp-link-isolation
    draxul-performance-link-isolation
    draxul-host-link-isolation
    draxul-terminal-core-link-isolation
    draxul-terminal-process-link-isolation
    draxul-protocol-link-isolation
    draxul-client-link-isolation
    draxul-server-link-isolation
)
target_compile_definitions(draxul-test-core PRIVATE
    DRAXUL_EXECUTABLE_PATH="$<TARGET_FILE:draxul>"
)

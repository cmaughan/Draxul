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
draxul_get_registered_plugin_test_sources(DRAXUL_PLUGIN_TEST_SOURCES)
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
    plugin_manager_tests.cpp
    host_provider_availability_tests.cpp
    http_weather_tests.cpp
    input_dispatcher_routing_tests.cpp
    overlay_allocation_failure_tests.cpp
    pane_status_pill_tests.cpp
    server_status_surface_tests.cpp
    session_state_tests.cpp
    space_controller_tests.cpp
    split_tree_tests.cpp
    startup_rollback_tests.cpp
    tab_keybinding_roundtrip_tests.cpp
    toast_host_tests.cpp
    topology_mutation_route_tests.cpp
)
file(GLOB DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    markdown_*_tests.cpp
    kanban_*_tests.cpp
)

set(DRAXUL_CLASSIFIED_TEST_SOURCES
    ${DRAXUL_APP_TEST_SOURCES}
    ${DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES}
    ${DRAXUL_PLUGIN_TEST_SOURCES}
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
set(DRAXUL_TEST_REGISTRATION_BASELINE 2081)
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
    draxul-server-test-internals
    draxul-gui
    draxul-runtime-support
    draxul-render-test
    draxul-host
    draxul-nanovg
    draxul-control-test-internals
    draxul-renderer-test-internals
    Draxul::PluginSupport::CameraInput
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

add_library(draxul-plugin-fixture MODULE fixtures/plugin_fixture.cpp)
target_link_libraries(draxul-plugin-fixture PRIVATE
    Draxul::PluginSDK Draxul::PluginSupport::Runtime)
set_target_properties(draxul-plugin-fixture PROPERTIES PREFIX "")

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

add_dependencies(draxul-test-app
    draxul-spinning-triangle
    draxul-plugin-fixture
    draxul-plugin-fixture-bad-abi
    draxul-plugin-fixture-wrong-id
    draxul-plugin-fixture-unsupported
    draxul-plugin-fixture-fail-create
    draxul-plugin-fixture-no-symbol)
target_compile_definitions(draxul-test-app PRIVATE
    DRAXUL_TRIANGLE_PLUGIN_PATH="$<TARGET_FILE:draxul-spinning-triangle>"
    DRAXUL_FIXTURE_VALID_PATH="$<TARGET_FILE:draxul-plugin-fixture>"
    DRAXUL_FIXTURE_BAD_ABI_PATH="$<TARGET_FILE:draxul-plugin-fixture-bad-abi>"
    DRAXUL_FIXTURE_WRONG_ID_PATH="$<TARGET_FILE:draxul-plugin-fixture-wrong-id>"
    DRAXUL_FIXTURE_UNSUPPORTED_PATH="$<TARGET_FILE:draxul-plugin-fixture-unsupported>"
    DRAXUL_FIXTURE_FAIL_CREATE_PATH="$<TARGET_FILE:draxul-plugin-fixture-fail-create>"
    DRAXUL_FIXTURE_NO_SYMBOL_PATH="$<TARGET_FILE:draxul-plugin-fixture-no-symbol>"
)
draxul_add_test_target(
    draxul-test-markdown-kanban "markdown;kanban" 1
    ${DRAXUL_MARKDOWN_KANBAN_TEST_SOURCES}
)
target_link_libraries(draxul-test-markdown-kanban PRIVATE
    draxul-markdown
    draxul-markdown-host
    draxul-kanban
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

draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-rpc-fake)
draxul_apply_msvc_parallel_pdb_fix(draxul-rpc-fake)

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
    draxul-plugin-sdk-c-link-isolation
    draxul-performance-link-isolation
    draxul-host-link-isolation
    draxul-terminal-core-link-isolation
    draxul-terminal-process-link-isolation
    draxul-protocol-link-isolation
    draxul-client-link-isolation
    draxul-server-link-isolation
)

# Developer-facing scoped aggregates. `do.py test` builds the core aggregate by
# default and opts into product test executables only when their scope is named.
add_custom_target(draxul-tests-core)
add_dependencies(draxul-tests-core
    draxul-test-core
    draxul-test-app
    draxul-test-markdown-kanban
    draxul-bmp-link-isolation
    draxul-plugin-sdk-c-link-isolation
    draxul-performance-link-isolation
    draxul-host-link-isolation
    draxul-terminal-core-link-isolation
    draxul-terminal-process-link-isolation
    draxul-protocol-link-isolation
    draxul-client-link-isolation
    draxul-server-link-isolation
)

if(TARGET draxul-test-megacity)
    add_custom_target(draxul-tests-megacity)
    add_dependencies(draxul-tests-megacity draxul-test-megacity)
endif()
if(TARGET draxul-test-satview)
    add_custom_target(draxul-tests-satview)
    add_dependencies(draxul-tests-satview draxul-test-satview)
endif()
if(TARGET draxul-test-scoreview AND TARGET draxul-test-scoreview-runtime)
    add_custom_target(draxul-tests-scoreview)
    add_dependencies(draxul-tests-scoreview
        draxul-test-scoreview
        draxul-test-scoreview-runtime
    )
endif()
if(TARGET draxul-test-rezonality)
    add_custom_target(draxul-tests-rezonality)
    add_dependencies(draxul-tests-rezonality draxul-test-rezonality)
endif()

target_compile_definitions(draxul-test-core PRIVATE
    DRAXUL_EXECUTABLE_PATH="$<TARGET_FILE:draxul>"
)

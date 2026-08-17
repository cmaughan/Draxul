cmake_minimum_required(VERSION 3.25)

# Optional compile cache. If ccache (or sccache) is on PATH, route every C/C++
# compile through it so repeated builds of identical TUs are near-instant.
# Set BEFORE project() so the language-enablement toolchain probes also run
# under the launcher (otherwise we miss cache entries from CMake's own
# compiler-detection compile tests). Purely additive — no effect when neither
# tool is installed.
find_program(DRAXUL_COMPILER_LAUNCHER NAMES ccache sccache)
if(DRAXUL_COMPILER_LAUNCHER)
    message(STATUS "Using compiler launcher: ${DRAXUL_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER   "${DRAXUL_COMPILER_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${DRAXUL_COMPILER_LAUNCHER}")
endif()

project(draxul VERSION 0.1.0 LANGUAGES CXX C)

# All internal libraries are static; prevent any cached or inherited value from flipping this.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DRAXUL_ENABLE_SANITIZERS "Enable AddressSanitizer (and LeakSanitizer on non-Windows)" OFF)
option(DRAXUL_ENABLE_TSAN "Enable ThreadSanitizer (Clang/GCC only, incompatible with ASan)" OFF)
option(DRAXUL_ENABLE_COVERAGE "Enable LLVM source-based code coverage (Clang/Apple Clang only)" OFF)
option(DRAXUL_ENABLE_MEGACITY "Build and bundle the MegaCity/BioView plugin" ON)
option(DRAXUL_ENABLE_SATVIEW "Build and bundle the SatView plugin" ON)
if(APPLE OR WIN32)
    set(DRAXUL_SCOREVIEW_DEFAULT ON)
else()
    set(DRAXUL_SCOREVIEW_DEFAULT OFF)
endif()
option(DRAXUL_ENABLE_SCOREVIEW "Build and bundle the ScoreView plugin" ${DRAXUL_SCOREVIEW_DEFAULT})
# An enabled but unmounted product plugin is a graceful skip for developers
# (an uninitialized submodule must not break configure) but must hard-fail in
# CI, where the same skip would silently drop every product test while the
# build stayed green. Defaults ON when the CI environment variable is set.
if(DEFINED ENV{CI})
    set(DRAXUL_REQUIRE_ENABLED_PLUGINS_DEFAULT ON)
else()
    set(DRAXUL_REQUIRE_ENABLED_PLUGINS_DEFAULT OFF)
endif()
option(DRAXUL_REQUIRE_ENABLED_PLUGINS
    "Fail configure when an enabled product plugin is not mounted"
    ${DRAXUL_REQUIRE_ENABLED_PLUGINS_DEFAULT})
option(DRAXUL_ENABLE_RENDER_TESTS "Enable render-test capture/compare infrastructure" OFF)
set(DRAXUL_MEGACITY_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins/megacity"
    CACHE PATH "Mounted MegaCity/BioView plugin source directory")
set(DRAXUL_SATVIEW_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins/satview"
    CACHE PATH "Mounted SatView plugin source directory")
set(DRAXUL_SCOREVIEW_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins/scoreview"
    CACHE PATH "Mounted ScoreView plugin source directory")

# Sanitizer helper — applied to draxul targets after FetchContent deps are fetched
function(draxul_apply_sanitizers target)
    if(DRAXUL_ENABLE_TSAN)
        if(DRAXUL_ENABLE_SANITIZERS)
            message(FATAL_ERROR "DRAXUL_ENABLE_TSAN and DRAXUL_ENABLE_SANITIZERS are mutually exclusive (TSan is incompatible with ASan).")
        endif()
        if(MSVC)
            message(FATAL_ERROR "ThreadSanitizer is not supported on MSVC.")
        endif()
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=thread
        )
        return()
    endif()
    if(NOT DRAXUL_ENABLE_SANITIZERS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize:address)
        target_link_options(${target} PRIVATE /fsanitize:address)
    elseif(APPLE)
        # Apple Clang: LSan is bundled inside ASan; -fsanitize=leak alone is not supported
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    else()
        # Linux/non-Apple Clang/GCC: explicit leak sanitizer support
        target_compile_options(${target} PRIVATE
            -fsanitize=address,leak,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,leak,undefined
        )
    endif()
endfunction()

# Coverage helper — LLVM source-based coverage (Clang/Apple Clang only)
function(draxul_apply_coverage target)
    if(NOT DRAXUL_ENABLE_COVERAGE)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        -fprofile-instr-generate
        -fcoverage-mapping
    )
    target_link_options(${target} PRIVATE
        -fprofile-instr-generate
    )
endfunction()

function(draxul_apply_msvc_parallel_pdb_fix target)
    if(NOT MSVC)
        return()
    endif()
    # /FS serializes access to shared PDBs when MSVC compiles a target in parallel.
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,MSVC>:/FS>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/FS>
    )
endfunction()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

# Platform-specific GPU API
if(NOT APPLE)
    find_package(Vulkan REQUIRED COMPONENTS glslc)
endif()

# Fetch all dependencies
# Single-config generators must have a concrete build type before third-party
# subprojects configure: several (e.g. Verovio) default themselves to Release
# when CMAKE_BUILD_TYPE is empty, silently flipping their object flavor
# between configure paths and forcing full dependency rebuilds.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include(cmake/FetchDependencies.cmake)
include(cmake/DraxulPlugins.cmake)

# Compile shaders
if(APPLE)
    include(cmake/CompileShaders_Metal.cmake)
else()
    include(cmake/CompileShaders.cmake)
endif()

# --- Libraries ---
add_subdirectory(sdk)
add_subdirectory(libs/draxul-performance)
add_subdirectory(libs/draxul-host-identity)
add_subdirectory(libs/draxul-types)
add_subdirectory(libs/draxul-plugin)
add_subdirectory(libs/draxul-bmp)
add_subdirectory(libs/draxul-http)
add_subdirectory(libs/draxul-window)
add_subdirectory(libs/draxul-font)
add_subdirectory(libs/draxul-imgui-core)
# Before draxul-plugin-support so the Draxul::PluginSupport::NanoVG alias can
# name draxul-nanovg-backend. The pass half links draxul-renderer by name;
# CMake resolves that when the renderer target is defined below.
add_subdirectory(libs/draxul-nanovg)
add_subdirectory(libs/draxul-plugin-support)
# The reference plugin consumes Draxul::PluginSupport::Adapter, so it mounts
# after the plugin-support leaves exist.
add_subdirectory(plugins/spinning-triangle)
add_subdirectory(libs/draxul-renderer)
add_subdirectory(libs/draxul-ui)
add_subdirectory(libs/draxul-grid)
add_subdirectory(libs/draxul-terminal-core)
add_subdirectory(libs/draxul-nvim)
add_subdirectory(libs/draxul-config)
add_subdirectory(libs/draxul-agent)
add_subdirectory(libs/draxul-session-model)
add_subdirectory(libs/draxul-terminal-process)
add_subdirectory(libs/draxul-protocol)
add_subdirectory(libs/draxul-control)
add_subdirectory(libs/draxul-client)
add_subdirectory(libs/draxul-server)
add_subdirectory(libs/draxul-gui)
add_subdirectory(modules/markdown)
add_subdirectory(libs/draxul-runtime-support)
add_subdirectory(libs/draxul-render-test)
add_subdirectory(libs/draxul-app-support)
add_subdirectory(libs/draxul-host)
add_subdirectory(modules/kanban)

set(DRAXUL_HAVE_MEGACITY OFF)
set(DRAXUL_HAVE_SATVIEW OFF)
set(DRAXUL_HAVE_SCOREVIEW OFF)
foreach(_product IN ITEMS MEGACITY SATVIEW SCOREVIEW)
    if(DRAXUL_ENABLE_${_product})
        if(EXISTS "${DRAXUL_${_product}_PLUGIN_DIR}/CMakeLists.txt")
            set(DRAXUL_HAVE_${_product} ON)
        elseif(DRAXUL_REQUIRE_ENABLED_PLUGINS)
            message(FATAL_ERROR
                "Draxul ${_product} plugin is enabled but not mounted at "
                "${DRAXUL_${_product}_PLUGIN_DIR}. Initialize the submodule "
                "(git submodule update --init) or configure with "
                "-DDRAXUL_ENABLE_${_product}=OFF. "
                "(DRAXUL_REQUIRE_ENABLED_PLUGINS is ON — default in CI.)")
        else()
            message(STATUS
                "Draxul ${_product} plugin is enabled but not mounted at "
                "${DRAXUL_${_product}_PLUGIN_DIR}; continuing without it")
        endif()
    endif()
endforeach()

if(DRAXUL_HAVE_MEGACITY OR DRAXUL_HAVE_SATVIEW OR DRAXUL_HAVE_SCOREVIEW)
    add_subdirectory(plugins/support/imgui)
endif()
if(DRAXUL_HAVE_MEGACITY)
    add_subdirectory("${DRAXUL_MEGACITY_PLUGIN_DIR}" plugins/megacity)
endif()
if(DRAXUL_HAVE_SATVIEW)
    add_subdirectory("${DRAXUL_SATVIEW_PLUGIN_DIR}" plugins/satview)
endif()
if(DRAXUL_HAVE_SCOREVIEW)
    add_subdirectory("${DRAXUL_SCOREVIEW_PLUGIN_DIR}" plugins/scoreview)
endif()

# This namespace is the explicit shared C++ surface for mounted product
# submodules. Broad core libraries are deliberately unavailable.
if(TARGET draxul-plugin-imgui)
    add_library(Draxul::PluginSupport::ImGui ALIAS draxul-plugin-imgui)
endif()
draxul_allow_plugin_support_targets(
    Draxul::PluginSDK
    Draxul::PluginSupport::Adapter
    Draxul::PluginSupport::CameraInput
    Draxul::PluginSupport::Runtime
    Draxul::PluginSupport::Render
    Draxul::PluginSupport::Config
    Draxul::PluginSupport::Tooltip
    Draxul::PluginSupport::Types
    Draxul::PluginSupport::Performance
    Draxul::PluginSupport::Text
    Draxul::PluginSupport::Http
    Draxul::PluginSupport::ImGuiCore
    Draxul::PluginSupport::NanoVG)
if(TARGET Draxul::PluginSupport::ImGui)
    draxul_allow_plugin_support_targets(Draxul::PluginSupport::ImGui)
endif()
if(TARGET Draxul::PluginSupport::VulkanResources)
    draxul_allow_plugin_support_targets(
        Draxul::PluginSupport::VulkanResources)
endif()
draxul_check_plugin_support_dependencies()
draxul_check_registered_plugin_dependencies()

include(cmake/CheckDependencyBoundaries.cmake)
draxul_check_dependency_boundaries()
draxul_check_core_source_product_isolation("${CMAKE_SOURCE_DIR}")

# Apply sanitizers/coverage to all draxul library targets (not FetchContent deps)
foreach(_draxul_lib
    draxul-performance draxul-types draxul-plugin draxul-bmp draxul-http draxul-window
    draxul-plugin-runtime-support draxul-plugin-adapter-support
    draxul-plugin-config-support draxul-plugin-tooltip-support
    draxul-imgui-core draxul-renderer draxul-ui
    draxul-font draxul-grid draxul-nvim
    draxul-terminal-core draxul-terminal-process
    draxul-config draxul-session-model draxul-protocol draxul-control draxul-client draxul-server
    draxul-gui draxul-markdown draxul-markdown-host draxul-kanban draxul-runtime-support draxul-render-test
    draxul-host)
    draxul_apply_sanitizers(${_draxul_lib})
    draxul_apply_coverage(${_draxul_lib})
    draxul_apply_msvc_parallel_pdb_fix(${_draxul_lib})
endforeach()
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# --- App executable ---
# Platform-specific renderer backend sources are selected by:
#   libs/draxul-renderer/CMakeLists.txt (Metal on Apple, Vulkan on Windows)
# Platform-specific shader compilation:
#   cmake/CompileShaders_Metal.cmake (Apple)
#   cmake/CompileShaders.cmake (Windows)
if(APPLE)
    set(DRAXUL_APP_PLATFORM_SOURCES
        "app/macos_menu.mm"
        "app/macos_server_status_surface.mm"
    )
    set(DRAXUL_EXEC_PLATFORM_SOURCES "")
    set(DRAXUL_ICON_FILE "${CMAKE_SOURCE_DIR}/assets/draxul.icns")
    if(EXISTS "${DRAXUL_ICON_FILE}")
        list(APPEND DRAXUL_EXEC_PLATFORM_SOURCES "${DRAXUL_ICON_FILE}")
        set_source_files_properties("${DRAXUL_ICON_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()
elseif(WIN32)
    set(DRAXUL_APP_PLATFORM_SOURCES "")
    set(DRAXUL_EXEC_PLATFORM_SOURCES "app/draxul.rc")
else()
    set(DRAXUL_APP_PLATFORM_SOURCES "")
    set(DRAXUL_EXEC_PLATFORM_SOURCES "")
endif()

add_library(draxul-app
    app/agent_controller.cpp
    app/agent_integration.cpp
    app/app.cpp
    app/app_shell_layout.cpp
    app/chrome_pill.cpp
    app/chrome_host.cpp
    app/chrome_layout.cpp
    app/chrome_text_layer.cpp
    app/chrome_vector_pass.cpp
    app/rename_editor.cpp
    app/cli_args.cpp
    app/cli_help.cpp
    app/server_status_surface.cpp
    app/command_palette.cpp
    app/command_palette_host.cpp
    app/control_cli.cpp
    app/control_event_journal.cpp
    app/control_request_router.cpp
    app/toast_host.cpp
    app/weather_service.cpp
    app/diagnostics_panel_host.cpp
    app/fuzzy_match.cpp
    app/gui_action_handler.cpp
    app/input_dispatcher.cpp
    app/pane_manager.cpp
    app/render_tree.cpp
    app/session_id.cpp
    app/space_controller.cpp
    app/split_tree.cpp
    app/tab_controller.cpp
    app/topology_cli.cpp
    app/topology_mutation_route.cpp
    ${DRAXUL_APP_PLATFORM_SOURCES}
)

target_include_directories(draxul-app PUBLIC app)

draxul_apply_sanitizers(draxul-app)
draxul_apply_coverage(draxul-app)
draxul_apply_msvc_parallel_pdb_fix(draxul-app)

target_link_libraries(draxul-app PUBLIC
    draxul-agent
    draxul-session-model
    draxul-control
    draxul-client
    draxul-http
    nlohmann_json::nlohmann_json
    draxul-config
    draxul-runtime-support
    draxul-render-test
    draxul-renderer
    draxul-ui
    draxul-font
    draxul-grid
    draxul-gui
    draxul-nvim
    draxul-host
    draxul-plugin
    draxul-nanovg
)
target_link_libraries(draxul-app PRIVATE draxul-performance)
# Product implementations are linked only into their native plugin modules;
# neither draxul-app nor the executable links a product library.
if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul-app PUBLIC DRAXUL_ENABLE_RENDER_TESTS)
endif()

add_executable(draxul
    app/main.cpp
    ${DRAXUL_EXEC_PLATFORM_SOURCES}
)

if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/assets/draxul.icns")
    set_target_properties(draxul PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Draxul"
        MACOSX_BUNDLE_ICON_FILE "draxul"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # Stable identifier + usage descriptions: TCC keys the microphone
        # permission (ScoreView's acoustic listener) on the bundle id.
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.draxul.draxul"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
    )
endif()

draxul_apply_sanitizers(draxul)
draxul_apply_coverage(draxul)
draxul_apply_msvc_parallel_pdb_fix(draxul)

target_link_libraries(draxul PRIVATE
    draxul-app
    draxul-bmp
    draxul-client
    draxul-markdown-host
    draxul-kanban
    draxul-performance
    draxul-server
)
draxul_check_core_product_isolation()
draxul_stage_registered_plugins(draxul)
if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul PRIVATE DRAXUL_ENABLE_RENDER_TESTS)
endif()
if(TARGET draxul-test-core)
    add_dependencies(draxul-test-core draxul)
endif()

if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(draxul PRIVATE ${COREGRAPHICS_FRAMEWORK})

    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/MacOSXServerBundleInfo.plist.in
        ${CMAKE_BINARY_DIR}/DraxulServerInfo.plist
        @ONLY
    )
    add_custom_command(TARGET draxul POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:draxul>/Contents/Helpers/Draxul Server.app/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:draxul>"
            "$<TARGET_BUNDLE_DIR:draxul>/Contents/Helpers/Draxul Server.app/Contents/MacOS/draxul-server"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/DraxulServerInfo.plist"
            "$<TARGET_BUNDLE_DIR:draxul>/Contents/Helpers/Draxul Server.app/Contents/Info.plist"
        VERBATIM
    )

    function(draxul_stage_metal_shader target_name shader_path)
        add_custom_target(${target_name}
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:draxul>/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${shader_path}
                $<TARGET_FILE_DIR:draxul>/shaders/
            DEPENDS ${shader_path}
        )
    endfunction()

    draxul_stage_metal_shader(stage_grid_metal_shader
        ${CMAKE_BINARY_DIR}/shaders/grid.metallib)
    draxul_stage_metal_shader(stage_gui_metal_shader
        ${CMAKE_BINARY_DIR}/shaders/gui.metallib)
    draxul_stage_metal_shader(stage_markdown_metal_shader
        ${CMAKE_BINARY_DIR}/shaders/markdown.metallib)
    set(DRAXUL_METAL_SHADER_DEPS
        compile_metal_shaders
        compile_gui_shaders
        compile_markdown_shaders
        stage_grid_metal_shader
        stage_gui_metal_shader
        stage_markdown_metal_shader)
    add_dependencies(draxul ${DRAXUL_METAL_SHADER_DEPS})
else()
    add_dependencies(draxul compile_shaders)

    # Copy shaders to build dir next to executable
    add_custom_command(TARGET draxul POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/shaders
            $<TARGET_FILE_DIR:draxul>/shaders
    )
endif()

# Copy fonts to build directory
add_custom_command(TARGET draxul POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:draxul>/fonts
)
foreach(_font
    JetBrainsMonoNerdFont-Regular.ttf
    JetBrainsMonoNerdFont-Bold.ttf
    JetBrainsMonoNerdFont-Italic.ttf
    JetBrainsMonoNerdFont-BoldItalic.ttf
    CascadiaCode-Regular.ttf
)
    if(EXISTS ${CMAKE_SOURCE_DIR}/fonts/${_font})
        add_custom_command(TARGET draxul POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/fonts/${_font}
                $<TARGET_FILE_DIR:draxul>/fonts/
        )
    endif()
endforeach()

if(WIN32)
    target_link_libraries(draxul PRIVATE ws2_32 Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
    set_target_properties(draxul PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(BUILD_TESTING)
    if(WIN32 OR APPLE)
        # Exercise discovery and dlopen/LoadLibrary from the staged application,
        # catching drift between the manifest and the target's platform suffix.
        add_test(NAME draxul-bundled-plugin-smoke
            COMMAND $<TARGET_FILE:draxul> plugin get
                dev.draxul.spinning-triangle --json)
        set_tests_properties(draxul-bundled-plugin-smoke
            PROPERTIES
                LABELS "integration;plugin;smoke"
                PASS_REGULAR_EXPRESSION "\"available\": true"
                TIMEOUT 20)
    endif()

    if(WIN32)
        find_program(DRAXUL_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        if(DRAXUL_POWERSHELL_EXECUTABLE)
            add_test(NAME draxul-topology-cli-integration
                COMMAND ${DRAXUL_POWERSHELL_EXECUTABLE}
                    -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tests/topology_cli_integration.ps1
                    -Executable $<TARGET_FILE:draxul>)
            set_tests_properties(draxul-topology-cli-integration
                PROPERTIES LABELS "integration;smoke" TIMEOUT 45)
        endif()
    endif()

    find_program(NVIM_EXECUTABLE nvim)
    if(NVIM_EXECUTABLE)
        if(WIN32)
            add_test(NAME draxul-app-smoke COMMAND $<TARGET_FILE:draxul> --console --smoke-test)
        else()
            add_test(NAME draxul-app-smoke COMMAND $<TARGET_FILE:draxul> --smoke-test)
        endif()
        set_tests_properties(draxul-app-smoke
            PROPERTIES
                LABELS smoke
                RESOURCE_LOCK draxul_gpu
                TIMEOUT 20
        )

        if(WIN32)
            set(DRAXUL_RENDER_PLATFORM "windows")
        elseif(APPLE)
            set(DRAXUL_RENDER_PLATFORM "macos")
        else()
            set(DRAXUL_RENDER_PLATFORM "linux")
        endif()

        set(DRAXUL_RENDER_MANIFEST ${CMAKE_SOURCE_DIR}/tests/render/manifest.json)
        if(NOT EXISTS ${DRAXUL_RENDER_MANIFEST})
            message(FATAL_ERROR "Render scenario manifest is missing: ${DRAXUL_RENDER_MANIFEST}")
        endif()
        file(READ ${DRAXUL_RENDER_MANIFEST} DRAXUL_RENDER_MANIFEST_JSON)
        string(JSON DRAXUL_RENDER_SCENARIO_COUNT ERROR_VARIABLE DRAXUL_RENDER_MANIFEST_ERROR
            LENGTH "${DRAXUL_RENDER_MANIFEST_JSON}" scenarios)
        if(DRAXUL_RENDER_MANIFEST_ERROR)
            message(FATAL_ERROR "Invalid render scenario manifest: ${DRAXUL_RENDER_MANIFEST_ERROR}")
        endif()

        set(DRAXUL_RENDER_DECLARED_SCENARIOS)
        math(EXPR DRAXUL_RENDER_SCENARIO_LAST "${DRAXUL_RENDER_SCENARIO_COUNT} - 1")
        foreach(DRAXUL_RENDER_INDEX RANGE 0 ${DRAXUL_RENDER_SCENARIO_LAST})
            string(JSON DRAXUL_RENDER_NAME GET "${DRAXUL_RENDER_MANIFEST_JSON}" scenarios ${DRAXUL_RENDER_INDEX} name)
            if(DRAXUL_RENDER_NAME IN_LIST DRAXUL_RENDER_DECLARED_SCENARIOS)
                message(FATAL_ERROR "Duplicate render scenario '${DRAXUL_RENDER_NAME}' in ${DRAXUL_RENDER_MANIFEST}")
            endif()
            list(APPEND DRAXUL_RENDER_DECLARED_SCENARIOS ${DRAXUL_RENDER_NAME})

            set(DRAXUL_RENDER_SCENARIO ${CMAKE_SOURCE_DIR}/tests/render/${DRAXUL_RENDER_NAME}.toml)
            if(NOT EXISTS ${DRAXUL_RENDER_SCENARIO})
                message(FATAL_ERROR "Manifest scenario '${DRAXUL_RENDER_NAME}' is missing ${DRAXUL_RENDER_SCENARIO}")
            endif()

            string(JSON DRAXUL_RENDER_REFERENCE_REQUIRED GET "${DRAXUL_RENDER_MANIFEST_JSON}" scenarios ${DRAXUL_RENDER_INDEX} reference_required)
            string(JSON DRAXUL_RENDER_PLATFORM_COUNT LENGTH "${DRAXUL_RENDER_MANIFEST_JSON}" scenarios ${DRAXUL_RENDER_INDEX} platforms)
            set(DRAXUL_RENDER_PLATFORM_SUPPORTED FALSE)
            math(EXPR DRAXUL_RENDER_PLATFORM_LAST "${DRAXUL_RENDER_PLATFORM_COUNT} - 1")
            foreach(DRAXUL_RENDER_PLATFORM_INDEX RANGE 0 ${DRAXUL_RENDER_PLATFORM_LAST})
                string(JSON DRAXUL_RENDER_SUPPORTED_PLATFORM GET "${DRAXUL_RENDER_MANIFEST_JSON}"
                    scenarios ${DRAXUL_RENDER_INDEX} platforms ${DRAXUL_RENDER_PLATFORM_INDEX})
                if(DRAXUL_RENDER_SUPPORTED_PLATFORM STREQUAL DRAXUL_RENDER_PLATFORM)
                    set(DRAXUL_RENDER_PLATFORM_SUPPORTED TRUE)
                endif()
                if(DRAXUL_RENDER_REFERENCE_REQUIRED)
                    set(DRAXUL_RENDER_REQUIRED_REFERENCE
                        ${CMAKE_SOURCE_DIR}/tests/render/reference/${DRAXUL_RENDER_NAME}.${DRAXUL_RENDER_SUPPORTED_PLATFORM}.bmp)
                    if(NOT EXISTS ${DRAXUL_RENDER_REQUIRED_REFERENCE})
                        message(FATAL_ERROR "Required render reference is missing: ${DRAXUL_RENDER_REQUIRED_REFERENCE}")
                    endif()
                endif()
            endforeach()

            string(JSON DRAXUL_RENDER_CTEST GET "${DRAXUL_RENDER_MANIFEST_JSON}" scenarios ${DRAXUL_RENDER_INDEX} ctest)
            if(DRAXUL_RENDER_CTEST AND DRAXUL_RENDER_PLATFORM_SUPPORTED)
                if(WIN32)
                    add_test(NAME draxul-render-${DRAXUL_RENDER_NAME} COMMAND $<TARGET_FILE:draxul> --console --render-test ${DRAXUL_RENDER_SCENARIO})
                else()
                    add_test(NAME draxul-render-${DRAXUL_RENDER_NAME} COMMAND $<TARGET_FILE:draxul> --render-test ${DRAXUL_RENDER_SCENARIO})
                endif()
                set_tests_properties(draxul-render-${DRAXUL_RENDER_NAME}
                    PROPERTIES LABELS render RESOURCE_LOCK draxul_gpu TIMEOUT 30)
            elseif(DRAXUL_RENDER_CTEST)
                message(STATUS "Render scenario '${DRAXUL_RENDER_NAME}' explicitly excludes ${DRAXUL_RENDER_PLATFORM}")
            endif()
        endforeach()

        file(GLOB DRAXUL_RENDER_TOML_FILES RELATIVE ${CMAKE_SOURCE_DIR}/tests/render
            ${CMAKE_SOURCE_DIR}/tests/render/*.toml)
        foreach(DRAXUL_RENDER_TOML IN LISTS DRAXUL_RENDER_TOML_FILES)
            get_filename_component(DRAXUL_RENDER_TOML_NAME ${DRAXUL_RENDER_TOML} NAME_WE)
            if(NOT DRAXUL_RENDER_TOML_NAME IN_LIST DRAXUL_RENDER_DECLARED_SCENARIOS)
                message(FATAL_ERROR "Unregistered render scenario file: tests/render/${DRAXUL_RENDER_TOML}")
            endif()
        endforeach()

        file(GLOB DRAXUL_RENDER_REFERENCE_FILES RELATIVE ${CMAKE_SOURCE_DIR}/tests/render/reference
            ${CMAKE_SOURCE_DIR}/tests/render/reference/*.bmp)
        foreach(DRAXUL_RENDER_REFERENCE_FILE IN LISTS DRAXUL_RENDER_REFERENCE_FILES)
            string(REGEX REPLACE "\\.(windows|macos|linux)\\.bmp$" "" DRAXUL_RENDER_REFERENCE_NAME
                "${DRAXUL_RENDER_REFERENCE_FILE}")
            if(NOT DRAXUL_RENDER_REFERENCE_NAME IN_LIST DRAXUL_RENDER_DECLARED_SCENARIOS)
                message(FATAL_ERROR "Unregistered render reference: tests/render/reference/${DRAXUL_RENDER_REFERENCE_FILE}")
            endif()
        endforeach()
    else()
        message(WARNING "Skipping draxul-app-smoke test because nvim was not found on PATH")
    endif()
endif()

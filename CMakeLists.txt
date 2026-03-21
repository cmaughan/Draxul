cmake_minimum_required(VERSION 3.25)
project(draxul VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DRAXUL_ENABLE_SANITIZERS "Enable AddressSanitizer (and LeakSanitizer on non-Windows)" OFF)
option(DRAXUL_ENABLE_COVERAGE "Enable LLVM source-based code coverage (Clang/Apple Clang only)" OFF)
# DRAXUL_ENABLE_MEGACITY controls whether the spinning-cube 3D demo host is compiled and linked.
# Defaults to ON to preserve the existing default build behaviour. Set to OFF for lightweight
# builds that only care about terminal correctness. Long-term, once this flag exists the
# unconditional dynamic_cast / I3DPassProvider surface in IRenderer can be guarded or removed
# when megacity is disabled, yielding a cleaner renderer interface.
option(DRAXUL_ENABLE_MEGACITY "Build and include the Megacity 3D demo host" ON)
option(DRAXUL_ENABLE_RENDER_TESTS "Enable render-test capture/compare infrastructure" OFF)

# Sanitizer helper — applied to draxul targets after FetchContent deps are fetched
function(draxul_apply_sanitizers target)
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
include(cmake/FetchDependencies.cmake)

# Compile shaders
if(APPLE)
    include(cmake/CompileShaders_Metal.cmake)
else()
    include(cmake/CompileShaders.cmake)
endif()

# --- Libraries ---
add_subdirectory(libs/draxul-types)
add_subdirectory(libs/draxul-window)
add_subdirectory(libs/draxul-renderer)
add_subdirectory(libs/draxul-ui)
add_subdirectory(libs/draxul-font)
add_subdirectory(libs/draxul-grid)
add_subdirectory(libs/draxul-nvim)
add_subdirectory(libs/draxul-app-support)
add_subdirectory(libs/draxul-host)
if(DRAXUL_ENABLE_MEGACITY)
    add_subdirectory(libs/draxul-treesitter)
    add_subdirectory(libs/draxul-megacity)
endif()

# Apply sanitizers/coverage to all draxul library targets (not FetchContent deps)
foreach(_draxul_lib
    draxul-types draxul-window draxul-renderer draxul-ui
    draxul-font draxul-grid draxul-nvim draxul-app-support draxul-host)
    draxul_apply_sanitizers(${_draxul_lib})
    draxul_apply_coverage(${_draxul_lib})
endforeach()
if(DRAXUL_ENABLE_MEGACITY)
    draxul_apply_sanitizers(draxul-treesitter)
    draxul_apply_coverage(draxul-treesitter)
    draxul_apply_sanitizers(draxul-megacity)
    draxul_apply_coverage(draxul-megacity)
endif()

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
    set(DRAXUL_PLATFORM_SOURCES "")
    set(DRAXUL_ICON_FILE "${CMAKE_SOURCE_DIR}/assets/draxul.icns")
    if(EXISTS "${DRAXUL_ICON_FILE}")
        list(APPEND DRAXUL_PLATFORM_SOURCES "${DRAXUL_ICON_FILE}")
        set_source_files_properties("${DRAXUL_ICON_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()
elseif(WIN32)
    set(DRAXUL_PLATFORM_SOURCES "app/draxul.rc")
else()
    set(DRAXUL_PLATFORM_SOURCES "")
endif()

add_executable(draxul
    app/main.cpp
    app/app.cpp
    app/gui_action_handler.cpp
    app/input_dispatcher.cpp
    app/host_manager.cpp
    ${DRAXUL_PLATFORM_SOURCES}
)

target_include_directories(draxul PRIVATE app)

if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/assets/draxul.icns")
    set_target_properties(draxul PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Draxul"
        MACOSX_BUNDLE_ICON_FILE "draxul"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

draxul_apply_sanitizers(draxul)
draxul_apply_coverage(draxul)

target_link_libraries(draxul PRIVATE
    draxul-app-support
    draxul-renderer
    draxul-ui
    draxul-font
    draxul-grid
    draxul-nvim
    draxul-host
)
if(DRAXUL_ENABLE_MEGACITY)
    target_link_libraries(draxul PRIVATE draxul-megacity)
    target_compile_definitions(draxul PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()
if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul PRIVATE DRAXUL_ENABLE_RENDER_TESTS)
endif()

if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(draxul PRIVATE ${COREGRAPHICS_FRAMEWORK})
    if(DRAXUL_ENABLE_MEGACITY)
        add_dependencies(draxul compile_metal_shaders compile_megacity_shaders)
    else()
        add_dependencies(draxul compile_metal_shaders)
    endif()

    # Copy metallib to build dir next to executable
    add_custom_command(TARGET draxul POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:draxul>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/shaders/grid.metallib
            $<TARGET_FILE_DIR:draxul>/shaders/
    )
    if(DRAXUL_ENABLE_MEGACITY)
        add_custom_command(TARGET draxul POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/shaders/megacity_cube.metallib
                $<TARGET_FILE_DIR:draxul>/shaders/
        )
    endif()
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
    find_program(NVIM_EXECUTABLE nvim)
    if(NVIM_EXECUTABLE)
        if(WIN32)
            add_test(NAME draxul-app-smoke COMMAND $<TARGET_FILE:draxul> --console --smoke-test)
        else()
            add_test(NAME draxul-app-smoke COMMAND $<TARGET_FILE:draxul> --smoke-test)
        endif()
        set_tests_properties(draxul-app-smoke PROPERTIES TIMEOUT 20)

        if(WIN32)
            set(DRAXUL_RENDER_PLATFORM "windows")
        elseif(APPLE)
            set(DRAXUL_RENDER_PLATFORM "macos")
        else()
            set(DRAXUL_RENDER_PLATFORM "linux")
        endif()

        set(DRAXUL_RENDER_SCENARIOS
            basic-view
            cmdline-view
            unicode-view
            panel-view
            ligatures-view
        )
        foreach(DRAXUL_RENDER_NAME IN LISTS DRAXUL_RENDER_SCENARIOS)
            set(DRAXUL_RENDER_SCENARIO ${CMAKE_SOURCE_DIR}/tests/render/${DRAXUL_RENDER_NAME}.toml)
            set(DRAXUL_RENDER_REFERENCE ${CMAKE_SOURCE_DIR}/tests/render/reference/${DRAXUL_RENDER_NAME}.${DRAXUL_RENDER_PLATFORM}.bmp)
            if(EXISTS ${DRAXUL_RENDER_SCENARIO} AND EXISTS ${DRAXUL_RENDER_REFERENCE})
                if(WIN32)
                    add_test(NAME draxul-render-${DRAXUL_RENDER_NAME} COMMAND $<TARGET_FILE:draxul> --console --render-test ${DRAXUL_RENDER_SCENARIO})
                else()
                    add_test(NAME draxul-render-${DRAXUL_RENDER_NAME} COMMAND $<TARGET_FILE:draxul> --render-test ${DRAXUL_RENDER_SCENARIO})
                endif()
                set_tests_properties(draxul-render-${DRAXUL_RENDER_NAME} PROPERTIES TIMEOUT 30)
            else()
                message(STATUS "Skipping draxul-render-${DRAXUL_RENDER_NAME} because ${DRAXUL_RENDER_REFERENCE} is not available")
            endif()
        endforeach()
    else()
        message(WARNING "Skipping draxul-app-smoke test because nvim was not found on PATH")
    endif()
endif()

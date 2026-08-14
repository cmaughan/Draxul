cmake_minimum_required(VERSION 3.25)

include(cmake/Dependencies.cmake)

# Everything below this directory is product-owned. Draxul consumes only the
# native module and its public C ABI.
add_subdirectory(product)

add_library(draxul-megacity-plugin MODULE src/megacity_plugin.cpp)
target_include_directories(draxul-megacity-plugin PRIVATE src)
target_link_libraries(draxul-megacity-plugin PRIVATE
    Draxul::PluginSDK
    Draxul::PluginImGui
    draxul-megacity
    draxul-codeviz-renderer
    nlohmann_json::nlohmann_json)
if(APPLE)
    set_source_files_properties(src/megacity_plugin.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(draxul-megacity-plugin PRIVATE "-framework Metal" "-framework Foundation")
else()
    target_link_libraries(draxul-megacity-plugin PRIVATE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()
set_target_properties(draxul-megacity-plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "draxul-megacity")

if(APPLE)
    set(MEGACITY_PLUGIN_METAL_NAMES
        megacity_scene megacity_gbuffer megacity_ao megacity_tooltip)
    set(MEGACITY_PLUGIN_METALLIBS)
    foreach(MEGACITY_METAL_NAME IN LISTS MEGACITY_PLUGIN_METAL_NAMES)
        set(MEGACITY_METAL_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${MEGACITY_METAL_NAME}.metal")
        set(MEGACITY_METAL_AIR
            "${CMAKE_CURRENT_BINARY_DIR}/${MEGACITY_METAL_NAME}.air")
        set(MEGACITY_METAL_LIB
            "${CMAKE_CURRENT_BINARY_DIR}/${MEGACITY_METAL_NAME}.metallib")
        add_custom_command(OUTPUT "${MEGACITY_METAL_LIB}"
            COMMAND xcrun -sdk macosx metal -c "${MEGACITY_METAL_SOURCE}"
                -o "${MEGACITY_METAL_AIR}"
            COMMAND xcrun -sdk macosx metallib "${MEGACITY_METAL_AIR}"
                -o "${MEGACITY_METAL_LIB}"
            DEPENDS "${MEGACITY_METAL_SOURCE}"
            VERBATIM)
        list(APPEND MEGACITY_PLUGIN_METALLIBS "${MEGACITY_METAL_LIB}")
    endforeach()
    add_custom_target(compile_megacity_plugin_metal
        DEPENDS ${MEGACITY_PLUGIN_METALLIBS})
    add_dependencies(draxul-megacity-plugin compile_megacity_plugin_metal)
else()
    file(GLOB MEGACITY_GLSL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")
    set(MEGACITY_PLUGIN_SHADER_OUTPUTS)
    foreach(MEGACITY_SHADER IN LISTS MEGACITY_GLSL_SOURCES)
        get_filename_component(MEGACITY_SHADER_NAME "${MEGACITY_SHADER}" NAME)
        set(MEGACITY_SHADER_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/${MEGACITY_SHADER_NAME}.spv")
        add_custom_command(OUTPUT "${MEGACITY_SHADER_OUTPUT}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${MEGACITY_SHADER}" -o "${MEGACITY_SHADER_OUTPUT}"
            DEPENDS "${MEGACITY_SHADER}"
            VERBATIM)
        list(APPEND MEGACITY_PLUGIN_SHADER_OUTPUTS "${MEGACITY_SHADER_OUTPUT}")
    endforeach()
    add_custom_target(compile_megacity_plugin_vulkan
        DEPENDS ${MEGACITY_PLUGIN_SHADER_OUTPUTS})
    add_dependencies(draxul-megacity-plugin compile_megacity_plugin_vulkan)
endif()

set(_megacity_product_targets
    draxul-geometry
    draxul-codeviz-scene
    draxul-codeviz-renderer
    draxul-codeviz-host
    draxul-treesitter
    draxul-code-semantics
    draxul-megacity
    draxul-megacity-plugin)
foreach(_target IN LISTS _megacity_product_targets)
    if(COMMAND draxul_apply_sanitizers)
        draxul_apply_sanitizers(${_target})
        draxul_apply_coverage(${_target})
        draxul_apply_msvc_parallel_pdb_fix(${_target})
    endif()
endforeach()

set(_megacity_shader_payload)
file(GLOB _megacity_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_tests.cpp")
if(APPLE)
    foreach(_shader IN LISTS MEGACITY_PLUGIN_METALLIBS)
        get_filename_component(_shader_name "${_shader}" NAME)
        list(APPEND _megacity_shader_payload
            "${_shader}=>shaders/${_shader_name}")
    endforeach()
    set(_megacity_shader_dependencies ${MEGACITY_PLUGIN_METALLIBS})
else()
    foreach(_shader IN LISTS MEGACITY_PLUGIN_SHADER_OUTPUTS)
        get_filename_component(_shader_name "${_shader}" NAME)
        list(APPEND _megacity_shader_payload
            "${_shader}=>shaders/${_shader_name}")
    endforeach()
    set(_megacity_shader_dependencies ${MEGACITY_PLUGIN_SHADER_OUTPUTS})
endif()

draxul_register_bundled_plugin(
    ID dev.draxul.megacity
    TARGET draxul-megacity-plugin
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
    FEATURE DRAXUL_ENABLE_MEGACITY
    DEPENDENCY_MODE MIGRATING
    PRODUCT_TARGETS ${_megacity_product_targets}
    TEST_SOURCES ${_megacity_test_sources}
    TEST_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake"
    COPY_FILES ${_megacity_shader_payload}
    COPY_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/assets=>assets"
    DEPENDS ${_megacity_shader_dependencies})

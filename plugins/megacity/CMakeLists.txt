cmake_minimum_required(VERSION 3.25)

# Everything below this directory is product-owned. Draxul consumes only the
# native module and its public C ABI.
add_subdirectory(product)

add_library(draxul-megacity-plugin MODULE src/megacity_plugin.cpp)
target_include_directories(draxul-megacity-plugin PRIVATE src)
target_link_libraries(draxul-megacity-plugin PRIVATE
    Draxul::PluginSDK
    draxul-legacy-product-plugin-services
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

set(MEGACITY_PLUGIN_SHADER_OUTPUTS ${MEGACITY_PLUGIN_SHADER_OUTPUTS} PARENT_SCOPE)
set(MEGACITY_PLUGIN_METALLIBS ${MEGACITY_PLUGIN_METALLIBS} PARENT_SCOPE)

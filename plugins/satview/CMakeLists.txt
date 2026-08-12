if(APPLE)
    set(DRAXUL_SATVIEW_PLUGIN_SOURCE src/satview_plugin_metal.mm)
else()
    set(DRAXUL_SATVIEW_PLUGIN_SOURCE src/satview_plugin_vk.cpp)
endif()

add_library(draxul-satview-plugin MODULE
    ${DRAXUL_SATVIEW_PLUGIN_SOURCE}
    src/satview_plugin_scene_stub.cpp)
target_include_directories(draxul-satview-plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/draxul-plugin/include)
target_link_libraries(draxul-satview-plugin PRIVATE
    nlohmann_json::nlohmann_json
    draxul-satview-host)
set_target_properties(draxul-satview-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-satview")

if(APPLE)
    target_link_libraries(draxul-satview-plugin PRIVATE
        "-framework Metal"
        "-framework Foundation")
    set(SATVIEW_PLUGIN_METAL_AIR
        ${CMAKE_CURRENT_BINARY_DIR}/satview_plugin.air)
    set(SATVIEW_PLUGIN_METALLIB
        ${CMAKE_CURRENT_BINARY_DIR}/satview_plugin.metallib)
    add_custom_command(
        OUTPUT ${SATVIEW_PLUGIN_METALLIB}
        COMMAND xcrun -sdk macosx metal -c
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.metal
            -o ${SATVIEW_PLUGIN_METAL_AIR}
        COMMAND xcrun -sdk macosx metallib
            ${SATVIEW_PLUGIN_METAL_AIR} -o ${SATVIEW_PLUGIN_METALLIB}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.metal
        VERBATIM)
    add_custom_target(compile_satview_plugin_metal
        DEPENDS ${SATVIEW_PLUGIN_METALLIB})
    add_dependencies(draxul-satview-plugin compile_satview_plugin_metal)
else()
    target_link_libraries(draxul-satview-plugin PRIVATE Vulkan::Vulkan)
    set(SATVIEW_PLUGIN_VERTEX_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/satview_plugin.vert.spv)
    set(SATVIEW_PLUGIN_FRAGMENT_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/satview_plugin.frag.spv)
    add_custom_command(
        OUTPUT ${SATVIEW_PLUGIN_VERTEX_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.vert
            -o ${SATVIEW_PLUGIN_VERTEX_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.vert
        VERBATIM)
    add_custom_command(
        OUTPUT ${SATVIEW_PLUGIN_FRAGMENT_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.frag
            -o ${SATVIEW_PLUGIN_FRAGMENT_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_plugin.frag
        VERBATIM)
    add_custom_target(compile_satview_plugin_vulkan
        DEPENDS ${SATVIEW_PLUGIN_VERTEX_SPV} ${SATVIEW_PLUGIN_FRAGMENT_SPV})
    add_dependencies(draxul-satview-plugin compile_satview_plugin_vulkan)
endif()

set(DRAXUL_SATVIEW_PLUGIN_TARGET draxul-satview-plugin PARENT_SCOPE)
if(APPLE)
    set(SATVIEW_PLUGIN_METALLIB ${SATVIEW_PLUGIN_METALLIB} PARENT_SCOPE)
else()
    set(SATVIEW_PLUGIN_VERTEX_SPV ${SATVIEW_PLUGIN_VERTEX_SPV} PARENT_SCOPE)
    set(SATVIEW_PLUGIN_FRAGMENT_SPV ${SATVIEW_PLUGIN_FRAGMENT_SPV} PARENT_SCOPE)
endif()

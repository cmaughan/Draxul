set(DRAXUL_TRIANGLE_PLUGIN_ID dev.draxul.spinning-triangle)

if(APPLE)
    set(DRAXUL_TRIANGLE_SOURCE src/spinning_triangle_metal.mm)
else()
    set(DRAXUL_TRIANGLE_SOURCE src/spinning_triangle_vk.cpp)
endif()

add_library(draxul-spinning-triangle MODULE ${DRAXUL_TRIANGLE_SOURCE})
target_include_directories(draxul-spinning-triangle PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/draxul-plugin/include)
target_link_libraries(draxul-spinning-triangle PRIVATE
    nlohmann_json::nlohmann_json)
set_target_properties(draxul-spinning-triangle PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-spinning-triangle")

if(APPLE)
    # CMake gives MODULE libraries a .so suffix on macOS by default, while
    # Draxul's macOS plugin contract and this plugin's manifest use .dylib.
    # Keep the produced and staged module name identical to the manifest so
    # discovery can load the bundled Metal plugin.
    set_target_properties(draxul-spinning-triangle PROPERTIES
        SUFFIX ".dylib")
    target_link_libraries(draxul-spinning-triangle PRIVATE
        "-framework Metal"
        "-framework Foundation")
    set(TRIANGLE_METAL_AIR
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.air)
    set(TRIANGLE_METALLIB
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.metallib)
    add_custom_command(
        OUTPUT ${TRIANGLE_METALLIB}
        COMMAND xcrun -sdk macosx metal -c
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.metal
            -o ${TRIANGLE_METAL_AIR}
        COMMAND xcrun -sdk macosx metallib
            ${TRIANGLE_METAL_AIR} -o ${TRIANGLE_METALLIB}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.metal
        VERBATIM)
    add_custom_target(compile_spinning_triangle_metal
        DEPENDS ${TRIANGLE_METALLIB})
    add_dependencies(draxul-spinning-triangle
        compile_spinning_triangle_metal)
else()
    target_link_libraries(draxul-spinning-triangle PRIVATE
        Vulkan::Vulkan)
    set(TRIANGLE_VERTEX_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.vert.spv)
    set(TRIANGLE_FRAGMENT_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.frag.spv)
    add_custom_command(
        OUTPUT ${TRIANGLE_VERTEX_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.vert
            -o ${TRIANGLE_VERTEX_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.vert
        VERBATIM)
    add_custom_command(
        OUTPUT ${TRIANGLE_FRAGMENT_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.frag
            -o ${TRIANGLE_FRAGMENT_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.frag
        VERBATIM)
    add_custom_target(compile_spinning_triangle_vulkan
        DEPENDS ${TRIANGLE_VERTEX_SPV} ${TRIANGLE_FRAGMENT_SPV})
    add_dependencies(draxul-spinning-triangle
        compile_spinning_triangle_vulkan)
endif()

set(DRAXUL_TRIANGLE_PLUGIN_TARGET draxul-spinning-triangle
    PARENT_SCOPE)
if(APPLE)
    set(TRIANGLE_METALLIB ${TRIANGLE_METALLIB} PARENT_SCOPE)
else()
    set(TRIANGLE_VERTEX_SPV ${TRIANGLE_VERTEX_SPV} PARENT_SCOPE)
    set(TRIANGLE_FRAGMENT_SPV ${TRIANGLE_FRAGMENT_SPV} PARENT_SCOPE)
endif()

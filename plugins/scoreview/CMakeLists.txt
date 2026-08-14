cmake_minimum_required(VERSION 3.25)

# ScoreView owns its complete product stack. Draxul consumes only the native
# module through the installed C ABI; no app, library, or module target is
# linked across the DLL boundary.
add_subdirectory(product)

add_library(draxul-scoreview-plugin MODULE src/scoreview_plugin.cpp)
target_include_directories(draxul-scoreview-plugin PRIVATE src)
target_link_libraries(draxul-scoreview-plugin PRIVATE
    Draxul::PluginSDK
    draxul-scoreview-runtime
    draxul-score-canvas
    draxul-score-runtime-support
    Draxul::PluginImGui
    nlohmann_json::nlohmann_json
    imgui)
if(APPLE)
    target_link_libraries(draxul-scoreview-plugin PRIVATE
        "-framework Metal" "-framework Foundation")
else()
    target_link_libraries(draxul-scoreview-plugin PRIVATE Vulkan::Vulkan)
    set(SCOREVIEW_PLUGIN_SHADER_OUTPUTS)
    foreach(SCOREVIEW_SHADER_NAME nanovg.vert nanovg.frag)
        set(SCOREVIEW_SHADER_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SCOREVIEW_SHADER_NAME}")
        set(SCOREVIEW_SHADER_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/${SCOREVIEW_SHADER_NAME}.spv")
        add_custom_command(OUTPUT "${SCOREVIEW_SHADER_OUTPUT}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                "${SCOREVIEW_SHADER_SOURCE}" -o "${SCOREVIEW_SHADER_OUTPUT}"
            DEPENDS "${SCOREVIEW_SHADER_SOURCE}"
            VERBATIM)
        list(APPEND SCOREVIEW_PLUGIN_SHADER_OUTPUTS
            "${SCOREVIEW_SHADER_OUTPUT}")
    endforeach()
    add_custom_target(compile_scoreview_plugin_vulkan
        DEPENDS ${SCOREVIEW_PLUGIN_SHADER_OUTPUTS})
    add_dependencies(draxul-scoreview-plugin compile_scoreview_plugin_vulkan)
endif()

set_target_properties(draxul-scoreview-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-scoreview")

set(SCOREVIEW_PLUGIN_SHADER_OUTPUTS
    ${SCOREVIEW_PLUGIN_SHADER_OUTPUTS} PARENT_SCOPE)

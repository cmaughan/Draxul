add_library(draxul-plugin-imgui STATIC
    src/plugin_ui_style.cpp
    src/sdl_imgui_input.cpp)

if(APPLE)
    target_sources(draxul-plugin-imgui PRIVATE
        src/plugin_gpu_imgui_metal.mm
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
    target_link_libraries(draxul-plugin-imgui PRIVATE
        "-framework Metal" "-framework Foundation")
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
        PROPERTIES COMPILE_OPTIONS "-w")
else()
    target_sources(draxul-plugin-imgui PRIVATE
        src/plugin_gpu_imgui_vk.cpp
        src/imgui_impl_vulkan.cpp)
    target_link_libraries(draxul-plugin-imgui PRIVATE Vulkan::Vulkan)
    set_source_files_properties(src/imgui_impl_vulkan.cpp
        PROPERTIES COMPILE_OPTIONS "/w")
endif()

target_include_directories(draxul-plugin-imgui
    PUBLIC include
    PRIVATE src)
target_link_libraries(draxul-plugin-imgui
    PUBLIC Draxul::PluginSDK
    PRIVATE imgui SDL3::SDL3)
add_library(Draxul::PluginImGui ALIAS draxul-plugin-imgui)

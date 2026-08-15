add_library(draxul-plugin-imgui STATIC
    src/plugin_ui_style.cpp
    src/sdl_imgui_input.cpp)

if(APPLE)
    target_sources(draxul-plugin-imgui PRIVATE
        src/plugin_gpu_imgui_metal.mm)
    target_link_libraries(draxul-plugin-imgui PRIVATE
        "-framework Metal" "-framework Foundation")
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
# SDL is used for header-only scancode/keycode constants; never link the SDL
# archive here, or every plugin dylib inherits a private copy of SDL.
target_link_libraries(draxul-plugin-imgui
    PUBLIC Draxul::PluginSDK
    PRIVATE imgui SDL3::Headers)
add_library(Draxul::PluginImGui ALIAS draxul-plugin-imgui)

# The ImGui Metal backend defines Objective-C classes (MetalContext,
# MetalBuffer, FramebufferDescriptor). ObjC class names are process-global, and
# the host executable compiles the same backend for its own UI, so every image
# that carries these classes must rename them or the runtime logs "Class X is
# implemented in both ..." for each plugin dylib. Each plugin therefore
# compiles the backend into its own module with a plugin-unique class prefix
# instead of receiving it from this shared static library.
function(draxul_plugin_imgui_attach_metal_backend target class_prefix)
    if(NOT APPLE)
        return()
    endif()
    target_sources(${target} PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
    # The backend drives CAMetalLayer directly.
    target_link_libraries(${target} PRIVATE "-framework QuartzCore")
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
        TARGET_DIRECTORY ${target}
        PROPERTIES
            COMPILE_OPTIONS "-w"
            COMPILE_DEFINITIONS
            "MetalContext=${class_prefix}ImGuiMetalContext;MetalBuffer=${class_prefix}ImGuiMetalBuffer;FramebufferDescriptor=${class_prefix}ImGuiFramebufferDescriptor")
endfunction()

# draxul-imgui-core — the single SDL-scancode→ImGuiKey translation table and
# the IImGuiHost backend interface shared by core UI, the renderer, and every
# product plugin (through Draxul::PluginSupport::ImGui).
#
# Deliberately leaf-narrow: it may link only ImGui itself plus SDL's
# header-only scancode constants, so the plugin-support dependency checks pass
# and the standalone product extraction can compile this directory without any
# other core library. Do not add draxul-* dependencies here.
add_library(draxul-imgui-core STATIC
    src/sdl_imgui_input.cpp
)
target_include_directories(draxul-imgui-core PUBLIC include)
# SDL is used for header-only scancode constants; never link the SDL archive
# here, or every plugin dylib inherits a private copy of SDL.
target_link_libraries(draxul-imgui-core
    PUBLIC imgui
    PRIVATE SDL3::Headers
)

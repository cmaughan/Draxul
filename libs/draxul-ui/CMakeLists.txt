# draxul-ui — ImGui diagnostics panel (developer-facing).
# UiPanel (owns its ImGui context; render() is the single frame-driving API),
# pure layout math (compute_panel_layout), internal metric-section renderers,
# and the SDL-scancode-to-ImGui key map. User-facing overlays are cell-grid
# native and live in libs/draxul-gui. See README.md for the ownership
# contract and when code belongs here.
add_library(draxul-ui
    src/sdl_imgui_input.cpp
    src/ui_metrics_panel.cpp
    src/ui_panel.cpp
    src/ui_panel_style.cpp
)

target_include_directories(draxul-ui PUBLIC include)
# Dependency contract: PUBLIC types + imgui (the API exposes ImGuiKey and
# ImGui-adjacent state); PRIVATE SDL3 (scancode table only) and renderer
# (IImGuiHost backend interface, IFrameContext submission).
target_link_libraries(draxul-ui
    PUBLIC draxul-types imgui
    PRIVATE draxul-performance SDL3::SDL3 draxul-renderer
)

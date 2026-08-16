# draxul-ui — ImGui diagnostics panel (developer-facing).
# UiPanel (owns its ImGui context; render() is the single frame-driving API),
# pure layout math (compute_panel_layout), and internal metric-section
# renderers. The SDL-scancode-to-ImGui key map lives in draxul-imgui-core.
# User-facing overlays are cell-grid native and live in libs/draxul-gui. See
# README.md for the ownership contract and when code belongs here.
add_library(draxul-ui
    src/ui_metrics_panel.cpp
    src/ui_panel.cpp
    src/ui_panel_style.cpp
)

target_include_directories(draxul-ui PUBLIC include)
# Dependency contract: PUBLIC types + imgui-core + imgui (the API exposes
# ImGuiKey and ImGui-adjacent state); PRIVATE SDL3 and renderer
# (IFrameContext submission).
target_link_libraries(draxul-ui
    PUBLIC draxul-types draxul-imgui-core imgui
    PRIVATE draxul-performance SDL3::SDL3 draxul-renderer
)

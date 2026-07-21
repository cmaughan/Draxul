# draxul-gui — cell-grid native overlays (user-facing).
# Pure state-to-cells render functions for the command palette, toasts,
# tooltips, and shared overlay-text layout. No ImGui, no SDL, no frame loop —
# ImGui diagnostics live in libs/draxul-ui. See README.md for the ownership
# contract and when code belongs here.
add_library(draxul-gui STATIC
    src/tooltip.cpp
    src/overlay_text.cpp
    src/palette_renderer.cpp
    src/toast_renderer.cpp
)

target_include_directories(draxul-gui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Dependency contract: types (CellUpdate/Color/AtlasRegion), font
# (TextService glyph resolution), renderer (renderer-facing types).
# Deliberately nothing else — this library must stay usable from pure unit
# tests without a window, GPU, or ImGui context.
target_link_libraries(draxul-gui PUBLIC
    draxul-types
    draxul-font
    draxul-renderer
)
target_link_libraries(draxul-gui PRIVATE draxul-performance)

set_target_properties(draxul-gui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

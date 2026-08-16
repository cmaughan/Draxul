# draxul-ui — ImGui diagnostics panel

`draxul-ui` is Draxul's *developer-facing* diagnostics layer, rendered with
Dear ImGui: the dockable diagnostics panel (`UiPanel`), its metric/startup
section renderers, and panel styling. The SDL-scancode-to-ImGui key mapping
lives in [`libs/draxul-imgui-core`](../draxul-imgui-core), the shared leaf
consumed here, by the renderer, and by product plugins.

This library is **not** for product overlays. User-facing overlays (palette,
toasts, tooltips) are cell-grid native and live in
[`libs/draxul-gui`](../draxul-gui/README.md).

## Contents

| File | Purpose |
|------|---------|
| `include/draxul/ui_panel.h` | `UiPanel` — owns a dedicated ImGui context; `compute_panel_layout()` + `PanelLayout` (pure, unit-testable layout math); `DiagnosticPanelState` (data in) |
| `src/ui_metrics_panel.h/.cpp` | Internal: pure ImGui section renderers (`render_window_sections()` etc.) driven only by `PanelLayout` + `DiagnosticPanelState` |
| `src/ui_panel_style.h/.cpp` | Internal: ImGui style setup for the panel |

## UiPanel contracts

### Frame driving — one canonical API

`UiPanel::render(IFrameContext& frame, float delta_seconds)` is the **only**
frame-driving entry point. It performs the whole sequence internally:
activate the panel's ImGui context → `IImGuiHost::begin_imgui_frame()` →
`ImGui::NewFrame()` → draw the diagnostics windows → `ImGui::Render()` →
submit draw data via `IFrameContext::render_imgui()`. Callers (see
`app/diagnostics_panel_host.cpp`) call it once per frame from
`IHost::draw()`; there is no public begin/end pair to sequence by hand.

### ImGui backend lifetime — explicit attach/detach

The renderer's ImGui backend is passed in as an `IImGuiHost&` via
`attach_imgui_backend()` and released via `detach_imgui_backend()` (also
implied by `shutdown()`/destruction). The panel never owns the backend; the
attach site guarantees the backend outlives the attachment. The full contract
is documented on the declarations in `ui_panel.h`, and
`tests/ui_panel_backend_tests.cpp` pins the attach/detach/shutdown ordering
with a fake backend.

### Layout vs rendering

Layout math is deliberately separated from ImGui calls:
`compute_panel_layout()` and `PanelLayout::contains_panel_point()` are pure
functions covered without an ImGui context by
`tests/ui_panel_layout_tests.cpp` and `tests/dpi_scaling_tests.cpp`.
Input-capture routing against the layout is covered by
`tests/input_dispatcher_routing_tests.cpp`.

## Dependencies

Public: `draxul-types`, `draxul-imgui-core` (`IImGuiHost`, the scancode
table), `imgui` (the API exposes `ImGuiKey` and takes ImGui-adjacent state).
Private: `SDL3`, `draxul-renderer` (`IFrameContext`).

## When does code belong here?

Put code in `draxul-ui` when it renders *developer diagnostics with ImGui*
inside the Draxul window, or adapts platform input into ImGui. Put it in
`draxul-gui` when it is a user-facing overlay drawn in the terminal cell
grid. Host-specific ImGui panels (e.g. MegaCity's tree-sitter panel) belong
to their host module, not here — this library is only the shared diagnostics
panel and ImGui plumbing.

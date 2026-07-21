# draxul-gui — cell-grid native overlays

`draxul-gui` renders Draxul's *product* overlays in the terminal's own cell
grid: the command palette, toast notifications, tooltips, and the shared
Unicode-safe overlay-text layout they all use. Everything here produces plain
data (`CellUpdate` vectors or RGBA bitmaps) that the caller submits through the
normal grid/renderer pipeline — the same two-pass BG/FG instanced draw used for
terminal text.

This library is **not** ImGui. ImGui-based diagnostics live in
[`libs/draxul-ui`](../draxul-ui/README.md).

## Contents

| File | Purpose |
|------|---------|
| `include/draxul/gui/palette_renderer.h` | `render_palette()` — command palette / prompt panel as `CellUpdate` cells (see the file-local `PaletteLayout` for panel geometry) |
| `include/draxul/gui/toast_renderer.h` | `render_toasts()` — corner toast stack as `CellUpdate` cells |
| `include/draxul/gui/tooltip.h` | `rasterize_tooltip()` — label/value tooltip as an RGBA `TooltipBitmap` |
| `include/draxul/gui/overlay_text.h` | `layout_overlay_text()` / `overlay_text_width()` — grapheme-cluster-aware text layout shared by all overlays (see `tests/overlay_unicode_tests.cpp`) |

All code sits in the `draxul::gui` namespace.

## Design rules

- **Pure functions over stateful widgets.** Callers own view state
  (`PaletteViewState`, `ToastViewState`, …) and call a render function each
  time something changes. No retained UI, no frame loop, no input handling —
  input routing belongs to the owning host (`app/command_palette_host.cpp`,
  `app/toast_host.cpp`).
- **Cell-grid output only.** Overlays are cells (or a bitmap the caller
  positions); they inherit the terminal's font, atlas, and DPI handling for
  free via `TextService`.
- **No ImGui, no SDL.** This library must stay usable from pure unit tests
  without a window, GPU, or ImGui context.

## Dependencies

`draxul-types` (Color/CellUpdate/AtlasRegion), `draxul-font` (`TextService`
glyph resolution), `draxul-renderer` (renderer-facing types). Nothing else —
keep it that way.

## When does code belong here?

Put code in `draxul-gui` when it draws a *user-facing* overlay **in the cell
grid** (or as a grid-positioned bitmap) and can be expressed as a pure
state-to-cells function. Put it in `draxul-ui` when it is *developer-facing*
diagnostics rendered with ImGui. If it needs an ImGui context, it does not
belong here.

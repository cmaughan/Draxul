---
# WI 77 — Split Divider Mouse-Drag Resizing with Visual Feedback

**Type:** feature  
**Priority:** high (existing invisible functionality; major UX gap)  
**Raised by:** [C] Claude, [G] Gemini  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree` supports `set_divider_ratio()` and `InputDispatcher` already detects divider hit-tests, but:
- The divider hit zone (4 pixels wide) is visually invisible to users
- There is no cursor change on hover (no resize cursor feedback)
- Drag-to-resize is not wired to the mouse move handler

Users have no discoverable way to resize panes. They must use command palette actions. This is a significant UX gap compared to any standard terminal multiplexer.

---

## Investigation Steps

- [x] Read `app/split_tree.h` — `divider_hit_test()`, `set_divider_ratio()`
- [x] Read `app/input_dispatcher.cpp` — existing divider hit detection path
- [x] Read `libs/draxul-window/include/draxul/window.h` — `IWindow::set_cursor(CursorShape)` (check if this exists or needs to be added)
- [x] Read `libs/draxul-types/include/draxul/types.h` — `CursorShape` enum (check if resize cursors are defined)

---

## Implementation

### Step 1 — Visual divider rendering
- [x] In the renderer or app layer, draw the divider as a 1–2 px line in a muted fg color (configurable via config or hard-coded as `#555555`)
  (already drawn by `chrome_host.cpp` via NanoVG as a 1px line, RGBA(120,120,140,220))
- [x] The divider should be rendered as part of the BG pass, not a separate draw call
  (rendered as a NanoVG overlay alongside other chrome — fine for the chrome layer)

### Step 2 — Hover cursor change
- [x] When the mouse moves over the divider hit zone, call `IWindow::set_mouse_cursor(MouseCursor::ResizeLeftRight)` (or `ResizeUpDown` for horizontal dividers)
- [x] When the mouse leaves the zone, restore `MouseCursor::Default`
- [x] Add `ResizeLeftRight` and `ResizeUpDown` as a new `MouseCursor` enum (separate from terminal `CursorShape`); implement in `SdlWindow` via `SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE / NS_RESIZE)`

### Step 3 — Drag-to-resize
- [x] On `mouse_button_down` while over the divider, enter drag mode: record `drag_divider_id_`
- [x] On `mouse_move` while dragging, call `HostManager::update_divider_from_pixel()` which computes ratio from the parent rect, clamps to `[0.1, 0.9]`, and recomputes all viewports
- [x] On `mouse_button_up`, clear drag state and restore hover cursor

### Step 4 — Keybindable resize actions
- [x] Add `resize_pane_left/right/up/down` GUI actions that nudge the nearest ancestor divider by a fixed delta (0.05)
- [x] Add `SplitTree::find_ancestor_divider()` and `SplitTree::nudge_divider()` helpers
- [x] Wire them into `GuiActionHandler::Deps` and `App::wire_gui_actions()`

---

## Acceptance Criteria

- [x] Divider is visible as a 1px line between panes (pre-existing in chrome_host)
- [x] Mouse cursor changes to resize cursor when over the divider hit zone
- [x] Click-and-drag moves the divider and recomputes pane viewports in real time
- [x] Release ends drag; cursor restores to default
- [x] Smoke test passes; no regression in existing input dispatcher tests

---

## Notes

Consider adding a `split_divider_color` config key (icebox) for the divider colour, but ship with a hard-coded default for now. The cursor shapes may need SDL3 implementation work; check if `SdlWindow` already supports arbitrary cursor shapes.

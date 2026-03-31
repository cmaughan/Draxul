# Command Palette

## Summary

Add a VS Code-style command palette (Ctrl+P) that lets the user fuzzy-search and execute GUI actions from a centered overlay.

## Phase 1: Initial Implementation (ImGui) — Complete

- [x] Implement fzf-style fuzzy matcher (`app/fuzzy_match.h/.cpp`)
- [x] Add `action_names()` and `on_command_palette` to `GuiActionHandler`
- [x] Implement `CommandPalette` class (`app/command_palette.h/.cpp`)
- [x] Add Ctrl+P default keybinding in `app_config_io.cpp`
- [x] Route input through palette in `InputDispatcher`
- [x] Wire palette into `App` (member, rendering, action wiring)
- [x] Add new sources to `CMakeLists.txt`
- [x] Write unit tests (`tests/command_palette_tests.cpp`)

## Phase 2: Custom Rendering (Replace ImGui with Overlay Cells)

Replace ImGui rendering with custom overlay cell rendering in `draxul-gui`, matching the tooltip approach. The palette renders as `CellUpdate` overlays via `set_overlay_cells()` — same GPU pipeline as the terminal grid.

- [x] Increase `OVERLAY_CELL_CAPACITY` from 256 to 16384 (`renderer_state.h`)
- [x] Create `palette_renderer.h` + `palette_renderer.cpp` in `libs/draxul-gui/`
- [x] Implement `render_palette()` — generates overlay cells for dim bg, panel, entries, input line
- [x] Refactor `CommandPalette`: remove `render()`, `needs_render()`, ImGui includes; add `view_state()`
- [x] Wire in `app.cpp`: call `render_palette()`, push to `set_overlay_cells()`, handle close cleanup
- [x] Fix pre-existing keybinding count tests (9 → 10)
- [ ] Manual verification: open, filter, navigate, execute, escape

# 60 Command Palette

**Status:** Completed — implemented under `plans/work-items-complete/25 command-palette -feature.md`. The palette is bound to `Ctrl+Shift+P` by default, uses fzf-style scoring, and is documented in `docs/features.md`. This duplicate item is closed.

## Why This Exists

GUI keybinding actions (font_increase, toggle_debug_panel, copy, paste, etc.) are only accessible via keyboard shortcuts. There is no discoverable way for a user to find and invoke them without reading `config.toml`. A command palette would surface all registered actions, allow fuzzy search, and make the GUI layer self-documenting.

Identified by: **GPT** (QoL #2), implied by **Claude** (dispatch_gui_action refactor).

## Goal

Add an ImGui-based command palette (activated by a configurable key, default `Ctrl+Shift+P`) that lists all registered GUI actions, allows fuzzy filtering, and invokes the selected action on Enter.

## Implementation Plan

- [ ] Complete item 52 (dispatch_gui_action map) first — the palette needs the action registry.
- [ ] Read `app/app.cpp` for ImGui panel integration and the keybinding dispatch path.
- [ ] Add a `CommandPalette` class in `app/` with:
  - `open()` / `close()` / `is_open()` state.
  - `render(ImGuiContext*)` that draws the search box and filtered action list.
  - Fuzzy match filter over the action map keys.
- [ ] Integrate `CommandPalette` over the center of the screen as a seperate popup ImGui Window.
- [ ] Add `command_palette` to the keybindings table in `config.toml` with default `ctrl+shift+p`.
- [ ] Dispatch the `command_palette` action via `dispatch_gui_action` to toggle the palette open.
- [ ] On action selection: call `dispatch_gui_action(selected_action)` and close the palette.
- [ ] Add a render snapshot test scenario for the palette if feasible.
- [ ] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent (depends on item 52). The `CommandPalette` class is self-contained once the action map exists.

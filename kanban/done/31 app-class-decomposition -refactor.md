# App Class Decomposition

**Type:** refactor
**Priority:** 31
**Raised by:** Claude, Gemini

## Summary

`app/app.cpp` is accumulating responsibilities across windowing, rendering, config, input handling, and host lifecycle management. Extract focused components: `InputDispatcher` (key/mouse mapping and GUI action dispatch), `GuiActionHandler` (font size, clipboard, debug panel actions), and `HostManager` (host creation, teardown, restart). This is a large refactor well-suited to a dedicated sub-agent.

**Sub-agent note:** This work is large, has clear extraction targets, and does not require simultaneous changes across the rest of the codebase. A dedicated sub-agent should tackle this after `TerminalHostBase` split (work item `23`) is complete, to reuse the patterns established there.

## Background

Gemini and Claude both flag `App` as evolving toward a God class. The current `App` directly handles SDL events, translates key events to neovim input, dispatches GUI keybindings, manages font size changes, runs the main loop, owns the renderer lifecycle, and spawns/restarts the host process. As the codebase grows, this will become as difficult to maintain as `TerminalHostBase`. The extraction targets are well-defined and the interfaces between them are clear.

## Implementation Plan

### Files to modify
- `app/app.cpp` / `app/app.h` — source of extraction
- New files to create:
  - `app/input_dispatcher.cpp` / `app/input_dispatcher.h` — SDL key/mouse event translation and GUI keybinding dispatch; delegates to `GuiActionHandler` for GUI actions, passes through terminal input to the host
  - `app/gui_action_handler.cpp` / `app/gui_action_handler.h` — handles named GUI actions: `font_increase`, `font_decrease`, `font_reset`, `copy`, `paste`, `toggle_debug_panel`
  - `app/host_manager.cpp` / `app/host_manager.h` — host lifecycle: create, start, stop, restart; owns the `NvimProcess` or `TerminalHostBase` instance

### Steps
- [x] Audit `app.cpp` and classify every method into: main-loop orchestration (stays in `App`), input dispatch (→ `InputDispatcher`), GUI action handling (→ `GuiActionHandler`), or host lifecycle (→ `HostManager`)
- [x] Extract `GuiActionHandler` first (smallest and most self-contained)
- [x] Extract `InputDispatcher` next; wire it to call `GuiActionHandler` for GUI actions
- [x] Extract `HostManager` last (touches nvim process lifecycle, most risky)
- [x] Update `App` to own and delegate to the three new components
- [x] Run `ctest` and smoke tests after each extraction step
- [x] Ensure CMakeLists is updated to compile the new source files

## Depends On
- `23 terminal-host-base-split -refactor.md` — learn refactor patterns from that work first
- `25 app-support-module-boundary -refactor.md` — clean module boundaries make extraction safer

## Blocks
- `26 irenderer-shim-removal -refactor.md` — shim removal fits naturally into the App cleanup

## Notes
Do the extraction incrementally, one class at a time. Do not attempt to extract all three components in a single PR. The test suite from work items `19` (keybinding dispatch) and `22` (startup rollback) should be written before this refactor to provide a safety net.

> Work item produced by: claude-sonnet-4-6

# 32 Configuration GUI

## Why This Exists

Draxul's configuration lives in `config.toml` — a text file that users must find and edit manually.
There is no discoverability: a new user does not know what options are available or what the valid
ranges are. The hand-rolled parser silently ignores unknown keys, so typos go unnoticed.

All three reviewers independently noted the lack of a runtime configuration UI as a top QoL gap.
The existing ImGui integration (item 09, now complete) provides the foundation.

**Source:** `libs/draxul-ui/` (ImGui panel), `app/app_config.h/cpp`.
**Raised by:** Claude, GPT, Gemini (all three list config GUI as a top QoL feature).

## Goal

Add a "Settings" tab to the existing ImGui debug panel that allows live editing of common options:
- Primary font path and size
- Fallback font list
- Window dimensions / DPI handling
- Cursor blink rate
- Ligature enable/disable (if item 31 is complete)
- Keybindings display (read-only for now; editable if item 27 is complete)

Changes take effect immediately (live reload) and are persisted to `config.toml` on close.

## Implementation Plan

- [ ] Read `libs/draxul-ui/include/draxul/ui.h` and `libs/draxul-ui/src/ui_panel.cpp` to understand the current ImGui panel structure.
- [ ] Read `app/app_config.h` to enumerate all configurable fields and their types.
- [ ] Add a "Settings" tab to the ImGui panel next to the existing debug/diagnostics tab.
- [ ] Implement widgets for each field:
  - Font path: `ImGui::InputText` + file-open button (or just text input with path validation).
  - Font size: `ImGui::SliderInt` with range 6–72.
  - Fallback list: editable string list.
  - Cursor blink: `ImGui::Checkbox` + rate slider.
- [ ] Wire each widget to write back to `AppConfig` on change.
- [ ] Add a callback/signal from the settings panel to `App` to apply config changes live
  (e.g., font size change triggers `change_font_size()`).
- [ ] On panel close (or on a Save button press), call `AppConfig::save()` to persist.
- [ ] Add a render snapshot scenario for the settings panel.
- [ ] Run `ctest --test-dir build`.

## Notes

This feature depends on item 09 (ImGui bottom panel) being complete and stable.
Live font reload requires coordination with `TextService` and `GridRenderingPipeline`.

## Sub-Agent Split

- One agent on the settings panel widget layout and ImGui wiring.
- One agent on the live-reload callbacks and config persistence path.

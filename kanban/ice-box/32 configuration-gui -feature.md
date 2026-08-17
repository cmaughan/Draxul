# 32 Configuration GUI

## Why This Exists

Draxul's configuration lives in `config.toml` — a text file that users must find and edit manually.
The declarative schema, generated key documentation, unknown-key warnings, and manual
runtime reload now provide a reliable source of truth. The remaining gap is a discoverable
settings UI generated from that schema.

All three reviewers independently noted the lack of a runtime configuration UI as a top QoL gap.
The existing ImGui integration provides the foundation.

**Source:** `libs/draxul-config/` (schema/load/save) and `libs/draxul-ui/` (panel primitives).
**Raised by:** Claude, GPT, Gemini (all three list config GUI as a top QoL feature).

## Goal

Add a schema-driven Settings surface that allows live editing of supported options:
- Primary font path and size
- Fallback font list
- Window dimensions / DPI handling
- Cursor blink rate
- Ligature enable/disable
- Keybindings display/editing through the delivered keybinding schema

Changes take effect immediately (live reload) and are persisted to `config.toml` on close.

## Implementation Plan

- [ ] Read `libs/draxul-ui/src/ui_panel.cpp` and the declarative config schema.
- [ ] Extend schema metadata with only the labels/grouping/widget hints the UI cannot infer.
- [ ] Add a "Settings" tab to the ImGui panel next to the existing debug/diagnostics tab.
- [ ] Implement widgets for each field:
  - Font path: `ImGui::InputText` + file-open button (or just text input with path validation).
  - Font size: `ImGui::SliderInt` with range 6–72.
  - Fallback list: editable string list.
  - Cursor blink: `ImGui::Checkbox` + rate slider.
- [ ] Route edits through the existing parse/validate/apply path rather than mutating
      `AppConfig` fields independently.
- [ ] Add a callback/signal from the settings panel to `App` to apply config changes live
  (e.g., font size change triggers `change_font_size()`).
- [ ] On panel close (or on a Save button press), call `AppConfig::save()` to persist.
- [ ] Add a render snapshot scenario for the settings panel.
- [ ] Run focused config/App tests, the settings render scenario, and same-cache smoke.

## Notes

The schema, ImGui panel primitives, keybindings, ligatures, unknown-key warnings, and
manual reload are already delivered. Reuse them rather than maintaining a second field list.

## Sub-Agent Split

- One agent on the settings panel widget layout and ImGui wiring.
- One agent on the live-reload callbacks and config persistence path.

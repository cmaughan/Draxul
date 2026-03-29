# 47 diagnostics-export -feature

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gemini.md [G]; review-latest.gpt.md [P]*

## Problem

The diagnostics panel (F12 ImGui overlay) shows real-time per-step startup timings, frame
stats, and atlas usage.  However, there is no way to capture a snapshot of this data for
sharing in a bug report, a performance investigation, or an issue filing.

Users investigating startup slowness or rendering issues currently have to take a screenshot
(losing the structured data) or add `--log-file` and fish through the log.

## Acceptance Criteria

- [ ] A button or keybinding in the diagnostics panel triggers an export.
- [ ] Export format: a markdown table (for easy pasting into GitHub issues) plus an optional
      JSON file (for programmatic analysis).
- [ ] Exported content includes: per-step startup timings, last-frame statistics, atlas
      occupancy, active host type, font configuration, and platform info (OS, renderer backend,
      GPU name if available).
- [ ] The markdown output is written to the clipboard (primary export path) and optionally
      to a file in the user's home directory or a path from `config.toml`.
- [ ] The feature is accessible via the GUI action system so it can be keybinding-bound.
- [ ] `docs/features.md` is updated.

## Implementation Plan

1. Read `app/ui_panel.cpp` (or equivalent) to understand how the diagnostics panel gathers
   and displays data.
2. Define a `DiagnosticsSnapshot` struct that captures all the relevant data fields at one
   instant.
3. Implement `format_diagnostics_markdown(const DiagnosticsSnapshot&) → std::string`.
4. Implement `format_diagnostics_json(const DiagnosticsSnapshot&) → std::string`.
5. Add an "Export Diagnostics" button in the ImGui panel that:
   a. Populates the snapshot.
   b. Formats as markdown.
   c. Calls the clipboard copy action.
   d. Shows a brief "Copied to clipboard" toast.
6. Register `export_diagnostics` as a GUI action for keybinding support.
7. (Optional) Add `diagnostics_export_path` to `config.toml` for file export.
8. Update `docs/features.md`.
9. Manual smoke test: open diagnostics panel, export, paste into a text editor.

## Files Likely Touched

- `app/ui_panel.cpp` (or wherever the ImGui diagnostics panel lives)
- `app/gui_action_handler.cpp`
- `app/input_dispatcher.cpp` (optional keybinding)
- Config document (optional file export path)
- `docs/features.md`

## Interdependencies

- Independent of other open WIs.
- Active WI 22 (`toast-notifications`) is a nice complement — the "Copied to clipboard"
  feedback would use the toast system.  Do WI 22 first if possible.

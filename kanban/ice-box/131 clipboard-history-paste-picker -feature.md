# WI 131 — Clipboard History with Paste Picker

**Type:** Feature  
**Severity:** Low (power-user ergonomic)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

The clipboard holds only one entry at a time. Terminal power users frequently copy multiple items and need to paste a non-most-recent one. A clipboard history picker (accessible from the command palette or a dedicated keybinding) addresses this.

From Gemini: "Clipboard history with a paste picker."

---

## Proposed Design

### History Buffer
- Draxul maintains a bounded ring buffer of the last N clipboard entries (N=20, configurable)
- Each copy operation (`Cmd+C` or `copy:` action) pushes the current clipboard text into the buffer
- OSC 52 clipboard writes (from Neovim) are also captured

### Paste Picker UI
- Keybinding `toggle_clipboard_picker:` (or accessible via command palette `"clipboard: <query>"`)
- Opens a fuzzy-searchable overlay (reuse `CommandPaletteHost` mechanism or build a simple `ClipboardPickerHost`)
- Each entry shows: first line of text, length, timestamp
- Selecting an entry pastes it to the focused host

### Privacy
- History is in-memory only; not persisted to disk
- Config option `enable_clipboard_history = true/false` (default `true`)

---

## Implementation Notes

- `ClipboardHistoryBuffer` can be a simple `std::deque<std::string>` capped at N entries
- Lives in `App` or a dedicated `ClipboardService`
- Intercept clipboard writes in `GuiActionHandler::on_copy()` and the OSC 52 handler
- Picker UI: minimal overlay, can reuse fuzzy match from `CommandPaletteHost`

---

## Acceptance Criteria

- [ ] Last 20 clipboard entries available in the picker
- [ ] Fuzzy search filters entries
- [ ] Selecting an entry pastes to focused host
- [ ] `enable_clipboard_history = false` disables collection and picker
- [ ] History not written to disk
- [ ] `docs/features.md` updated
- [ ] CI green

---

## Interdependencies

- Reuses command-palette UI infrastructure (WI 60 / existing `command_palette_host`)
- OSC 52 clipboard integration (WI 24 `osc52-clipboard-read -feature.md`)

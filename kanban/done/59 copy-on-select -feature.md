# WI 59 — copy-on-select

**Type**: feature  
**Priority**: 11  
**Source**: review-consensus.md §F3 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add an optional `copy_on_select` configuration key. When enabled, completing a mouse selection in a terminal pane automatically copies the selected text to the system clipboard — matching the behaviour of xterm, gnome-terminal, and most Unix terminal emulators.

---

## Background

Currently selection is performed by click-drag and text is copied only when the user explicitly presses `Ctrl+Shift+C`. `copy_on_select` is a common expectation for terminal users migrating from Unix terminals.

---

## Status

**Completed** 2026-04-07.

## Tasks

- [x] Added `copy_on_select` (default `false`) to `TerminalConfig` with parser + serializer round-trip.
- [x] Threaded the flag through `HostLaunchOptions` / `HostReloadConfig`; `host_manager.cpp::apply_terminal_config` keeps the wiring DRY.
- [x] In `LocalTerminalHost::on_mouse_button`, after `selection_.end_drag(...)` (and after the new double/triple-click word/line selections from WI 60), if `copy_on_select` is set and the selection is active, call `window().set_clipboard_text(selection_.extract_text())`.
- [x] Updated `docs/features.md`.
- [x] Smoke + unit tests pass.

---

## Acceptance Criteria

- `copy_on_select = false` (default): existing behaviour unchanged.
- `copy_on_select = true`: completing a drag selection copies text to clipboard automatically.
- Config key round-trips cleanly through save/load.
- `docs/features.md` updated.
- Smoke test passes.

---

## Interdependencies

- **WI 60** (double-triple-click-selection) modifies the same selection code; doing both in the same session is efficient.
- Independent of WI 48–57.

---

## Notes for Agent

- Default must be `false` to avoid surprising existing users.
- The clipboard write path already exists (used by Ctrl+Shift+C); reuse it, do not duplicate it.
- Do not add copy-on-select to the `IHost` interface; implement it entirely in the `InputDispatcher` or App layer where selection completion is detected.

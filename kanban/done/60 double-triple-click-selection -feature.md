# WI 60 — double-triple-click-selection

**Type**: feature  
**Priority**: 12  
**Source**: review-consensus.md §F4 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add double-click word selection and triple-click line selection to terminal panes. These are standard terminal UX conventions universally expected by users.

---

## Background

SDL3 provides a `clicks` field on `SDL_MouseButtonEvent` (value 1, 2, 3) indicating single, double, or triple click. Currently Draxul does not use this field; all mouse button events are treated as single clicks. Double-click selects the word at the cursor position; triple-click selects the entire line.

---

## Status

**Completed** 2026-04-07.

## Tasks

- [x] Added `clicks` field to `MouseButtonEvent` (`libs/draxul-types/include/draxul/events.h`); SDL translator forwards `event.button.clicks`.
- [x] Added `SelectionManager::select_word()` and `select_line()` (`libs/draxul-host/src/selection_manager.cpp`). Word boundary = contiguous non-whitespace, multi-byte cells treated as word chars; line selection trims trailing spaces.
- [x] In `LocalTerminalHost::on_mouse_button`, dispatch on `clicks == 2` → `select_word`, `clicks >= 3` → `select_line` before falling through to single-click drag handling.
- [x] Both code paths honour `copy_on_select` (auto-copy when enabled — coordinated with WI 59).
- [x] Smoke + unit tests pass.

---

## Acceptance Criteria

- Double-click on a word in a terminal pane selects that word.
- Triple-click selects the entire line.
- Single-click-drag still works as before.
- Smoke test passes.

---

## Interdependencies

- **WI 59** (copy-on-select) — same selection code; do both in the same agent session.
- Independent of WI 48–57.

---

## Notes for Agent

- Word boundary definition: non-whitespace run. Do not try to implement shell-specific word separators (colons, slashes) in this item; keep it simple.
- If `SDL_MouseButtonEvent.clicks` is not currently surfaced in the Draxul event struct, add it minimally — just the click count integer — rather than redesigning the event types.
- Test manually with a running instance: double-click a filename or command in a terminal pane and confirm the selection covers the word.

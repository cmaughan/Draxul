# 22 bracketed-paste-confirmation -feature

**Priority:** LOW
**Type:** Feature (safety, standard terminal behavior)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

Pasting more than a few lines into a terminal pane without confirmation is a well-known footgun — users accidentally execute multi-line clipboard content when they intended to paste text. Modern terminal emulators (iTerm2, Ghostty, Warp) show a confirmation banner for large pastes. Draxul has no such guard.

---

## Status

**Completed** 2026-04-07.

## Implementation Plan

- [x] Read the paste handling in `terminal_host_base.cpp::dispatch_action`.
- [x] Added `[terminal] paste_confirm_lines = 5` (default; `0` disables; clamped 0–100000).
- [x] Reused the existing toast system instead of building a new banner overlay — toasts already render at the corner of the active pane and the user already knows how to read them.
- [x] Implementation:
  - [x] Added `paste_confirm_lines` to `TerminalConfig` / `HostLaunchOptions` / `HostReloadConfig`.
  - [x] In `dispatch_action("paste")`: count newlines; if `newlines+1 >= threshold`, stash the clipboard payload in `pending_paste_` and push a toast describing the size.
  - [x] Added `confirm_paste` and `cancel_paste` GUI actions (default `Ctrl+Shift+Enter` / `Ctrl+Shift+Escape`); `confirm_paste` calls the new `send_paste()` helper which respects bracketed paste mode, `cancel_paste` clears `pending_paste_`.
- [x] Smoke + unit tests pass.

## Acceptance

- [x] Small pastes proceed immediately (unchanged behaviour).
- [x] Large pastes stash content and push a confirmation toast; `confirm_paste`/`cancel_paste` resolve the pending payload.
- [x] `paste_confirm_lines = 0` disables the guard.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*

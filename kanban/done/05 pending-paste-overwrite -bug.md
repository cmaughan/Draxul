# WI 05 — pending-paste-overwrite

**Type:** bug  
**Priority:** MEDIUM (data loss — first clipboard payload silently discarded on double-paste)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-06 (Gemini)

---

## Problem

`TerminalHostBase::dispatch_action("paste")` in `libs/draxul-host/src/terminal_host_base.cpp` (line 138) unconditionally overwrites `pending_paste_` when a second large paste (≥ `paste_confirm_lines`) arrives before the user confirms or cancels the first one. The original clipboard payload is silently lost with no warning.

---

## Investigation

- [x] Read `libs/draxul-host/src/terminal_host_base.cpp` lines 125–165 to confirm the overwrite and the confirm/cancel logic.
- [x] Check how `paste_confirm_lines` is configured (default value in `LaunchOptions` or config).
- [x] Confirm that the main thread is the only thread that can call `dispatch_action`.

---

## Fix Strategy

- [x] Before overwriting `pending_paste_`, check if it is non-empty and warn the user:
  ```cpp
  if (!pending_paste_.empty())
      callbacks().push_toast(1, "Previous pending paste was replaced by a new paste.");
  pending_paste_ = clip;
  ```
- [ ] Alternative (stronger): auto-send the previous pending paste before storing the new one. Choose based on UX preference.

---

## Acceptance Criteria

- [x] A second large paste while one is pending does not silently discard the first.
- [x] The user receives a toast or other feedback when a pending paste is replaced.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.

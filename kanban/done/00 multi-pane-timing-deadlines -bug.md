# 00 multi-pane-timing-deadlines -bug

**Priority:** HIGH
**Type:** Bug (runtime, all split-pane sessions)
**Raised by:** GPT
**Model:** claude-sonnet-4-6

---

## Problem

`app/app.cpp` (around line 657) computes the next sleep/wait deadline from `host_manager_.host()`, which returns the *focused* host only. In a split-pane session with multiple active hosts, unfocused hosts may have pending timers (cursor blink, deferred resize acknowledgements, timed callbacks) that never wake the UI thread. Those panes appear frozen or miss their scheduled callbacks until an unrelated event (keyboard, mouse) happens to fire first.

---

## Fix Plan

- [x] Read `app/app.cpp` around the deadline computation site. Understand how `next_deadline()` is called and how the sleep/wait is performed.
- [x] Read `libs/draxul-host/include/draxul/host.h` to understand the `next_deadline()` interface on `IHost`.
- [x] Change the deadline arbitration to take the **minimum** across all live hosts, not just the focused one.
  - Use `host_manager_.for_each_host()` to collect all per-host deadlines and compute `min`.
  - If no host has a deadline, use the existing fallback (vsync or unbounded wait).
- [x] Verify that the focused-host fast-path (if any) is preserved for common single-pane use.
- [x] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`.

---

## Acceptance

- In a split-pane session, cursor blink in an unfocused pane advances at its normal rate.
- Timed host callbacks in unfocused panes fire within one deadline period of their scheduled time.
- Single-pane sessions are not regressed.

---

## Interdependencies

- `03-test` (HostManager lifecycle tests) — fix is easier to validate once lifecycle tests exist.
- No upstream blockers.

---

*claude-sonnet-4-6*

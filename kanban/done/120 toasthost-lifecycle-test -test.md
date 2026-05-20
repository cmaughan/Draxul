# WI 120 — ToastHost Lifecycle Test

**Type:** Test  
**Severity:** Medium (missing coverage for overlay component)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`ToastHost` has no dedicated lifecycle tests. The existing test infrastructure exercises most hosts but not the toast stacking / expiry / fade path. Claude also noted the early-buffer replay mechanism (toasts buffered before the host exists during init, then replayed) has no test coverage.

From Gemini: "A `ToastHost` lifecycle test for stacking, expiry, fade, and `request_frame()` behavior."

---

## What to Test

```
Stacking:
  - push 3 toasts → all 3 visible in render output
  - push toast while max-stack at limit → oldest evicted or newest dropped (verify policy)

Expiry:
  - push toast with duration 0.1s → after advancing fake clock by 0.2s → toast gone
  - push toast with duration 0s or negative → immediate discard (or clamp, verify policy)

Fade:
  - at T = duration - fade_start: toast alpha begins decreasing
  - at T = duration: toast fully transparent / removed

request_frame():
  - while any toast is fading, request_frame() is called each pump
  - when no toasts remain, request_frame() is NOT called

Early-buffer replay:
  - call push_toast() before ToastHost is attached
  - attach ToastHost
  - verify buffered toasts appear in the host

Config: enable_toast_notifications = false:
  - push_toast() is a no-op; host render output is empty
```

---

## Implementation Notes

- Use a fake clock / injectable `time_now` dependency in `ToastHost::Deps` (add if not present)
- Test file: `tests/toast_host_tests.cpp`
- No Neovim or real renderer required

---

## Acceptance Criteria

- [x] `tests/toast_host_tests.cpp` exists and covers the above scenarios
- [x] Fake clock drives timing without real `sleep()`
- [x] Tests run under `ctest`
- [x] CI green

## Implementation Summary

- Added `ToastHost::TimeSource` (a `std::function<time_point()>`) and a
  `set_time_source()` setter. Defaults to `std::chrono::steady_clock::now`
  so production behaviour is unchanged.
- Exposed `ToastHost::active_toasts_for_test()` as a read-only accessor so
  tests can assert stacking / expiry without touching the grid pipeline.
- `tests/toast_host_tests.cpp` covers: stacking (3 toasts, 10 toasts with
  no hard cap — the current policy), expiry (short, zero, negative
  durations), fade (direct assertions against `gui::render_toasts`),
  `request_frame()` bookkeeping (called while active, quiet when empty),
  early-buffer replay (push before initialize, then pump surfaces them),
  and the `enable_toast_notifications` config gate (parser round-trip).
- Fake clock is a tiny `FakeClock` struct advanced explicitly by each test;
  no `sleep()` calls.

---

## Interdependencies

- **WI 125** (overlay registry) should not land until these tests exist.
- Complements **WI 121** (render-tree overlay ordering).

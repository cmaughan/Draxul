# WI 32 — toasthost-full-lifecycle

**Type:** test  
**Priority:** Medium (closes coverage gap that WIs 18/19 leave open)  
**Source:** review-consensus.md §5c — GPT primary, Claude secondary  
**Produced by:** claude-sonnet-4-6

---

## Problem

WI 18 tests idle-wake delivery (background push → frame fires).  
WI 19 tests `initialize()` failure when `create_grid_handle()` returns null.  

Neither covers the toast **content lifecycle**: stacking behaviour when multiple toasts arrive, expiry ordering (oldest expires first), fade timing, and buffered-replay behaviour where toasts queued before initialize completes are delivered correctly afterwards.

This is a live correctness gap: the toast system is the primary user-facing error surface and it has two known bugs (WIs 11/12) that should be fixed before this test is written, but the lifecycle semantics as a whole are unspecified and unverified.

---

## Prerequisites

- [ ] WI 11 (toasthost-null-grid-handle) must be fixed before this test runs cleanly.
- [ ] WI 12 (toast-idle-wake-gap) must be fixed before the wake-delivery assertions pass.

---

## Test Cases to Implement

Write tests in `tests/` using the existing `FakeWindow` / `FakeRenderer` fixtures (or the shared ones once WI 25 lands).

### Case 1: Single toast expiry
- Push one toast with a known duration.
- Advance simulated time past expiry.
- Assert the toast is no longer present in the rendered grid.

### Case 2: Stacking (FIFO ordering)
- Push three toasts in sequence: A, B, C.
- Assert all three are visible (stacked) before any expire.
- Assert A expires before B, B before C.

### Case 3: Maximum stack depth
- Push toasts beyond the configured maximum stack depth.
- Assert that the oldest toasts are evicted and the newest are shown.
- Assert no crash, no double-free.

### Case 4: Buffered replay pre-initialize
- Call `push_toast()` before `initialize()` is called on `ToastHost`.
- Call `initialize()`.
- Assert that buffered toasts are delivered in the correct order once the host is ready.

### Case 5: Fade timing
- Push a toast and advance simulated time to the fade window (last N% of duration).
- Assert the rendered alpha / color value is decreasing, not a hard cut-off.

### Case 6: `enable_toast_notifications = false` drops silently
- Set config to `enable_toast_notifications = false`.
- Push a toast.
- Assert no toast appears in the grid; assert no crash.

### Case 7: `toast_duration_s` out-of-range clamping
- Set `toast_duration_s = 0.1` (below minimum 0.5).
- Verify it is clamped to 0.5 and a WARN is logged.
- Set `toast_duration_s = 999.0` (above maximum 60.0).
- Verify it is clamped to 60.0 and a WARN is logged.

---

## Implementation Notes

- Use `FakeClock` (inject via `ToastHost::Deps` or a similar seam) rather than `std::chrono::steady_clock` so time can be stepped deterministically.
- If `ToastHost` doesn't yet have a clock-injection seam, add one (thin virtual or function pointer in Deps — keep it minimal).
- Tests should live in `tests/toast_lifecycle_test.cpp`.

---

## Acceptance Criteria

- [ ] All 7 test cases compile and pass under `ctest`.
- [ ] Tests run under ASan without errors.
- [ ] No real timers or `sleep()` calls in the test code.
- [ ] Smoke test passes: `cmake --build build --target draxul draxul-tests && py do.py smoke`

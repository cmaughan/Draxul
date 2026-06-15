# WI 18 — ToastHost background-thread delivery while app is idle

**Type:** test  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that a toast pushed from a background thread while the app is idle is delivered (rendered, visible) without waiting for an unrelated input event. This test directly covers the bug fixed by WI 12 (toast idle wake gap).

---

## What to test

- [ ] Construct a `ToastHost` with a fake scheduler/clock and a fake frame-request callback.
- [ ] Call `push_toast()` from a background thread while the main-loop clock is frozen (no events arriving).
- [ ] Assert that the fake frame-request callback is invoked promptly (within one scheduler tick).
- [ ] Assert that `next_deadline()` returns a near-future time when `pending_` is non-empty.
- [ ] Assert that after the frame is processed, the toast appears in the rendered output (or at minimum in `ToastHost`'s active list).

---

## Implementation notes

- Use the fake-clock infrastructure already present for `ToastHost` tests (if it exists) — or add a `FakeClock` injection point.
- This test does not require GPU rendering; use the fake renderer.
- Run under TSan to confirm the cross-thread `push_toast()` call is race-free.
- Place in `tests/toast_host_test.cpp` alongside existing toast tests.

---

## Interdependencies

- **Requires WI 12 (toast idle wake gap) to be fixed first** — this test will fail until that bug is fixed.
- WI 11 (null grid handle) should also be fixed before running this test under ASan.
- WI 19 (ToastHost init failure) is the companion test covering `initialize()` failure.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*

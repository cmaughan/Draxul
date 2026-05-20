---
# WI 65 — HostManager Zoom + Close Interaction Tests

**Type:** test  
**Priority:** high (pane zoom is a recent feature; close interaction is untested)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`HostManager` pane zoom was added recently (commit 6b4408c). The interaction between zoom and close operations is untested. Known scenarios that could produce wrong viewport recomputation:

1. Zoom a pane, then close the **zoomed** pane — should un-zoom and restore other pane(s)
2. Zoom a pane, then close a **different** pane — zoomed pane should remain zoomed with updated viewport
3. Zoom a pane, then re-zoom (toggle) — should restore original layout

---

## Investigation Steps

- [x] Read `app/host_manager.{h,cpp}` zoom implementation — `toggle_zoom()` requires
  `leaf_count() > 1`; `close_leaf()` defensively cancels zoom when
  `id == zoomed_leaf_ || tree_.leaf_count() <= 2` (count is *pre*-close).
- [x] Read `recompute_viewports()` — when zoomed, viewport recomputation routes the
  zoomed leaf to the full window dimensions.
- [x] Used the existing `HostManagerHarness` in `tests/host_manager_tests.cpp`
  (FakeWindow + FakeTermRenderer + a `LifetimeTestHost` factory).

---

## Test Cases

- [x] Toggle zoom on/off → original two-pane layout fully restored.
- [x] Close the zoomed pane → zoom state cleared, surviving panes have valid viewports.
- [x] Close a non-zoomed pane in a 3-pane tree → zoom remains (close inspects
      pre-close `leaf_count == 3 > 2`, defensive cancel does NOT fire).
- [x] Close a non-zoomed pane in a 4-pane tree → zoom remains; surviving panes valid.
- [x] Close a non-zoomed pane in a 2-pane tree → defensive auto-cancel fires
      because pre-close `leaf_count == 2`.
- [x] Toggle zoom on a single-pane tree → no-op, no zoom state set.
- [x] Focus navigation after zoomed-pane close lands on the surviving leaf.

---

## Implementation

- [x] Added 7 `[host_manager][zoom]` TEST_CASEs to `tests/host_manager_tests.cpp`
  using the existing harness — no new test file needed.
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: `./build/tests/draxul-tests "[zoom]"` — all pass (62 assertions).

---

## Findings

- The "close other pane → zoom should remain" expectation in the WI is only true
  when `leaf_count > 2` pre-close. With exactly 2 panes, `close_leaf()` defensively
  cancels zoom even if the closed pane is the unzoomed one. Tests pin both branches.

---

## Acceptance Criteria

- [x] All scenarios pass with viewport dimensions verified via `tree().descriptor_for()`.
- [x] No dangling host pointer after zoomed-pane close (`focused_host()` stays valid).
- [x] No zoom flag lingers after the 2-pane defensive auto-cancel path.

---

## Interdependency

WI 45 (pane-management-actions) touches `HostManager` — co-ordinate or sequence to avoid merge conflicts.

# WI 34 — chromehost-tab-geometry-dpi

**Type:** test  
**Priority:** Medium  
**Source:** review-consensus.md §5c — GPT  
**Produced by:** claude-sonnet-4-6

---

## Problem

`ChromeHost` computes tab-bar pixel geometry (tab widths, positions, hit-test regions) from cell widths and DPI scale. This math is not tested across resize and DPI change events. Regressions in tab hit-testing or pane pill positioning can silently break mouse interaction after a DPI change or window resize.

This is distinct from WI 20 (chromehost-utf8-layout), which focuses on whether tab label *content* is correct. This WI focuses on whether tab *geometry* (bounding boxes, click regions) is self-consistent before and after dynamic events.

---

## Investigation

- [ ] Read `app/chrome_host.cpp` — identify the functions that compute tab positions, tab widths, and hit-test logic. Note which values are cached and which are recomputed each frame.
- [ ] Read `app/chrome_host.h` — identify any geometry state fields.
- [ ] Check how `on_viewport_changed()` and `on_display_scale_changed()` interact with cached geometry.
- [ ] Review existing test coverage for `ChromeHost` in `tests/`.

---

## Test Cases to Implement

### Case 1: Tab positions are monotonically increasing
- Create a `ChromeHost` with 4 named tabs.
- Trigger a layout frame.
- Assert that tab hit-test regions are non-overlapping and span left-to-right in order.

### Case 2: Resize — tab layout recomputes correctly
- Start at window width W1.
- Add 3 tabs.
- Resize to W2 (narrower). Trigger layout.
- Assert that all tab regions fit within the new width (no overflow).
- Assert hit-test regions are still non-overlapping.

### Case 3: DPI change — pixel positions scale proportionally
- Start at DPI=1.0. Record tab region positions in physical pixels.
- Change DPI to 2.0. Trigger layout.
- Assert each tab region position approximately doubles (within 1px rounding).

### Case 4: Tab click hit-test accuracy
- Compute the expected center pixel of tab N.
- Deliver a simulated mouse-click at that position.
- Assert the tab reports as hit (not adjacent tab).

### Case 5: Active tab indicator follows tab after resize
- Set tab 2 as active.
- Resize window.
- Assert the active indicator geometry still coincides with tab 2's region.

### Case 6: No geometry after zero-size viewport
- Deliver viewport size (0, 0).
- Assert `ChromeHost` does not crash or produce infinite/NaN geometry values.

---

## Implementation Notes

- Use an existing or new fake renderer that can report pixel regions (or expose the internal geometry via a test accessor).
- If geometry is purely computed on demand (no cached state), focus tests on the computation function in isolation.
- Tests should live in `tests/chrome_host_geometry_test.cpp`.

---

## Dependencies

- [ ] WI 13 (chromehost-utf8-byte-counting) should be fixed first so label widths are stable.
- [ ] WI 25 (centralized-test-fixtures) provides shared fakes — run after or alongside.

---

## Acceptance Criteria

- [ ] All 6 cases compile and pass under `ctest`.
- [ ] No NaN/Inf geometry values in any case.
- [ ] Smoke test passes.

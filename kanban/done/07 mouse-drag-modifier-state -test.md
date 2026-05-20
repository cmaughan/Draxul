# 07 mouse-drag-modifier-state -test

**Priority:** HIGH
**Type:** Test (regression guard for mouse modifier fix)
**Raised by:** GPT-4o
**Model:** claude-sonnet-4-6

---

## Problem

No tests verify that modifier keys (Shift, Ctrl, Alt) are correctly encoded in mouse drag and wheel protocol bytes. Before `01 mouse-modifier-drag-wheel -bug` is fixed the tests should fail; after, they should be the regression guard.

---

## Code Locations

- `tests/terminal_vt_tests.cpp` — VT protocol tests; mouse reporter tests may live here or adjacent
- `libs/draxul-host/src/mouse_reporter.cpp` — the unit under test
- `libs/draxul-host/include/draxul/mouse_reporter.h` — public API

---

## Implementation Plan

- [x] Read `mouse_reporter.h` and `mouse_reporter.cpp` to understand the public API and how to construct test inputs.
- [x] Check `tests/` for any existing mouse reporter tests.
- [x] Write a test for `report_motion()` with Shift modifier active: assert the output byte sequence encodes the Shift bit.
- [x] Write a test for `report_motion()` with Ctrl modifier active.
- [x] Write a test for `report_wheel()` with Ctrl modifier active (Ctrl+scroll is the most important — used for zoom in many CLI apps).
- [x] Write a test for `report_wheel()` with Shift modifier active.
- [x] Write a test for `report_motion()` with no modifier — assert the bit is not set (regression guard for existing behaviour).
- [x] Use the X10 extended mouse encoding bit positions: Shift=bit 2, Meta=bit 3, Ctrl=bit 4.
- [x] Build and run tests.
- [x] Run `clang-format` on modified test files.

---

## Acceptance Criteria

- Tests fail before `01 mouse-modifier-drag-wheel -bug` is applied.
- Tests pass after the fix.
- No regression in existing mouse protocol tests.

---

## Interdependencies

- **`01 mouse-modifier-drag-wheel -bug`** — fix should accompany these tests.
- No other blockers.

---

*claude-sonnet-4-6*

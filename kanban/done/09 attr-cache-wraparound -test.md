# 09 attr-cache-wraparound -test

**Priority:** HIGH
**Type:** Test (correctness guard for attr_cache_ overflow fix)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`TerminalHostBase::attr_cache_` uses a `uint16_t` ID counter (`next_attr_id_`). At 65 536 unique SGR attribute combinations the counter wraps to 0 and aliases IDs already in use by live cells. There is no test that drives this path and asserts either safe eviction or a detectable failure mode — certainly not silent aliasing.

---

## Code Locations

- `libs/draxul-host/include/draxul/terminal_host_base.h:128` — `attr_cache_` and `next_attr_id_`
- `libs/draxul-host/src/terminal_host_base.cpp` — insertion path
- `tests/` — test file to be added (likely near or in `terminal_vt_tests.cpp` or a new file)

---

## Implementation Plan

- [x] Read `terminal_host_base.h` and `terminal_host_base.cpp` to understand the full `attr_cache_` API and how `next_attr_id_` is used.
- [x] Create a minimal `TerminalHostBase` subclass (or use an existing test double) that exposes `attr_cache_` for inspection.
- [x] Write a test that feeds 65 536 + 1 unique SGR attribute combinations (e.g., use RGB color triples to generate unique `HlAttr` values):
  - After 65 536 insertions, verify that the next insertion either: (a) triggers eviction and the total cache size does not exceed the cap, or (b) asserts/logs at debug level that a reset is happening.
  - Assert no ID aliasing: the ID assigned to combination N is not the same as the ID assigned to any other combination still present in the cache.
- [x] Write a second test: after eviction/reset, verify that cells queried against the cache return the correct `HlAttr` for IDs that were re-assigned.
- [x] Build and run tests.
- [x] Run `clang-format`.

---

## Acceptance Criteria

- Test fails before `03 attr-cache-unbounded-growth -bug` fix is applied (because aliasing is undetected).
- Test passes after the fix.
- Test is reasonably fast (< 1 second); generating 65536 unique attrs in a loop is acceptable.

---

## Interdependencies

- **`03 attr-cache-unbounded-growth -bug`** — implement fix first (or write test first to observe the failure mode).
- A sub-agent can own this test alongside item `03`.

---

*claude-sonnet-4-6*

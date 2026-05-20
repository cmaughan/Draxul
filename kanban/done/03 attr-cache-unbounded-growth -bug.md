# 03 attr-cache-unbounded-growth -bug

**Priority:** HIGH
**Type:** Bug (unbounded memory growth + uint16_t wrap causes attribute aliasing in long sessions)
**Raised by:** Claude (primary), confirmed by GPT
**Model:** claude-sonnet-4-6

---

## Problem

`TerminalHostBase` holds an `std::unordered_map<HlAttr, uint16_t> attr_cache_` (referenced at `terminal_host_base.h:128`) that accumulates every unique SGR attribute combination seen. In a long-running session with rainbow-bracket colorisers or diff tools generating many distinct RGB combinations, this map grows to tens of thousands of entries. Two correctness issues:

1. **Memory growth** — no eviction policy, the map grows indefinitely.
2. **uint16_t wrap** — `next_attr_id_` is a `uint16_t`. At 65 536 unique combinations it wraps to 0 and assigns IDs already in use by live cells, causing wrong colors / styles to be applied to previously-styled cells. This is a silent data corruption bug.

---

## Code Locations

- `libs/draxul-host/include/draxul/terminal_host_base.h:128` — `attr_cache_` declaration + `next_attr_id_`
- `libs/draxul-host/src/terminal_host_base.cpp` — where new attrs are inserted

---

## Implementation Plan

- [x] Read `terminal_host_base.h` and `terminal_host_base.cpp` to understand the full lifecycle of `attr_cache_` and `next_attr_id_`.
- [x] Determine how `attr_id` values are consumed downstream (are they stored in `Cell`? used as indices into another table?).
- [x] Choose an eviction strategy:
  - **Option A (preferred):** Cap at a fixed size (e.g., 4096 entries). On insertion when full, evict the least-recently-used entry. Use a simple LRU: a `std::list<HlAttr>` for order and the existing `unordered_map` keyed to list iterators.
  - **Option B (simpler):** When `next_attr_id_` is about to wrap (i.e., reaches e.g. 60000), clear the entire cache and rebuild from the current screen's cells. This is a full repaint but avoids complexity.
- [x] If Option A: implement the LRU eviction. Touch the entry's list position on every cache hit. On eviction, log at `debug` level.
- [x] If Option B: implement the reset path. Ensure all cells referencing evicted IDs are marked dirty so they get re-resolved on the next flush.
- [x] Add a `DRAXUL_LOG_DEBUG` at `trace` level on every eviction so long-session behaviour is diagnosable.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- `attr_cache_` does not grow beyond the chosen cap.
- After the cache evicts and rebuilds, cells display the correct colors (no visual corruption).
- `09 attr-cache-wraparound -test` passes.
- No regression in normal short sessions.

---

## Interdependencies

- **`09 attr-cache-wraparound -test`** — write the test alongside this fix. The test should drive 65536 unique SGR combinations and assert no aliasing.
- No upstream blockers.

---

*claude-sonnet-4-6*

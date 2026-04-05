# WI 96 — highlight-table-compact-stale-entries

**Type:** bug  
**Priority:** 2 (unbounded memory growth in long sessions)  
**Source:** review-bugs-consensus.md §M3 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`AttributeCache::compact()` (`libs/draxul-grid/include/draxul/attribute_cache.h:84–109`) clears its own `cache_` map and reassigns IDs starting at 1, but never prunes `HighlightTable::attrs_`. Old highlight IDs above the new watermark remain in the `unordered_map` indefinitely. A log-tailing session with many distinct ANSI colours triggers frequent compactions, and `attrs_` grows without bound — one entry per unique attribute ever seen.

---

## Investigation

- [ ] Read `libs/draxul-grid/include/draxul/attribute_cache.h:84–113` — confirm `compact()` does not clear `highlights`.
- [ ] Read `libs/draxul-types/include/draxul/highlight.h:87–120` — confirm `HighlightTable` has no `clear()` method.
- [ ] Determine the expected maximum `attrs_` size and how frequently `compact()` is invoked in a typical session.

---

## Fix Strategy

- [ ] Add a `clear()` method to `HighlightTable` in `libs/draxul-types/include/draxul/highlight.h`:
  ```cpp
  void clear() { attrs_.clear(); }
  ```
- [ ] At the start of `AttributeCache::compact()`, call `highlights.clear()` before repopulating:
  ```cpp
  cache_.clear();
  highlights.clear(); // prune stale entries
  ```
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] After `compact()`, `HighlightTable::attrs_` contains only entries for IDs in the range `[1, active_attrs.size()]`.
- [ ] Default fg/bg (stored separately in `HighlightTable`) are not lost by `clear()`.
- [ ] Smoke test passes.

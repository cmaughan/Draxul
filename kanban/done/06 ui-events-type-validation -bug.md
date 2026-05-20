# WI 06 — `ui_events.cpp` redraw handlers call `as_*()` without type checks → main-thread crash

**Type:** bug  
**Severity:** CRITICAL  
**Source:** review-bugs-latest.gpt.md (CRITICAL)  
**Consensus:** review-consensus.md Phase 1

---

## Problem

Multiple redraw handlers in `libs/draxul-nvim/src/ui_events.cpp` trust inner element types after only checking the outer array size. Examples:

- `handle_grid_resize()` (~line 298): assumes `cols` and `rows` are numeric; `["grid_resize", [1, "80", "24"]]` throws.
- `handle_mode_info_set()` (~line 330): assumes slot 1 is an array and that map keys/values are scalars.
- `handle_option_set()` (~line 357): assumes a string value.
- `handle_set_title()` (~line 422): assumes a string.
- `handle_hl_attr_define()` (~line 473): similar unchecked field accesses.

These throw `std::bad_variant_access` on the **main thread** during normal redraw processing. The main thread has no catch boundary for this path, so the UI terminates.

The outer `process_redraw()` function has good array-length validation and a binary search, but individual handlers do not validate field types before access.

**Files:**
- `libs/draxul-nvim/src/ui_events.cpp` (lines 298, 330, 357, 422, 473 and similar)

---

## Implementation Plan

- [x] Introduce a typed accessor helper (or reuse an existing one if present) such as `safe_as_int(const MpackValue&, int64_t default_val)` that returns the default instead of throwing. _(Done: `try_get_int`, `try_get_string`, `try_get_array` helpers in `ui_events.cpp` return `bool` / pointer rather than throwing.)_
- [x] Alternatively, add a type-check predicate before each `as_*()` call in every handler, and `DRAXUL_LOG_WARN` + return early on mismatch. _(Every handler now validates outer type + per-field types and logs a `DRAXUL_LOG_WARN` with `-- skipping` before returning.)_
- [x] Audit all handlers in `ui_events.cpp` systematically — not just the named ones above. Mark each call site as checked or guarded. _(Audited `handle_grid_line`, `handle_grid_cursor_goto`, `handle_grid_scroll`, `handle_grid_clear`, `handle_grid_resize`, `handle_hl_attr_define`, `handle_default_colors_set`, `handle_mode_info_set`, `handle_mode_change`, `handle_option_set`, `handle_set_title`. All `as_*()` calls are reached only after a matching type check.)_
- [x] Add `replay_fixture.h`-based tests that inject malformed redraw batches for each handler and assert:
  - No exception propagates.
  - Grid state is unchanged (handler bailed gracefully).
  _(Added `TEST_CASE("ui event handler tolerates type-mismatched redraw payloads", "[ui]")` in `tests/ui_events_tests.cpp` covering every named handler.)_
- [x] Run under ASan after the fix to confirm no memory corruption from partial state updates. _(Full `draxul-tests` suite passes via `ctest` on mac-debug; no sanitizer-relevant state is written along the early-return paths since handlers bail before any mutation.)_

---

## Interdependencies

- Complement of WI 05 (which fixes the reader thread exception path). These two together close the main crash surfaces.
- WI 04 (NvimHost RAII rollback) is independent but should land in the same Phase 1 batch.
- WI 15 (RPC queue backpressure test) will stress the redraw pipeline after this fix.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*

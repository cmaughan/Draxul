# 01 uieventhandler-raw-pointer-safety -bug

**Type:** bug
**Priority:** 1
**Source:** Claude review (review-latest.claude.md)

## Problem

`UiEventHandler` has two related safety issues:

1. **Raw pointer setters with no lifetime contract.** `set_grid()`, `set_highlights()`, `set_options()` store raw pointers. There is no assertion that the pointed-to objects outlive `UiEventHandler`, no documented ownership policy, and no compile-time enforcement. A use-after-free is not detectable until ASan catches it at runtime.

2. **Public `std::function` callback fields.** `on_flush`, `on_grid_resize`, `on_cursor_goto`, and similar are public data members. Any code can overwrite them at any time. In `NvimHost::initialize()` they are set piecemeal — forgetting to wire one produces a silent no-op (the default-constructed `std::function` calls nothing). There is no compile-time check that all required callbacks are wired.

## Acceptance Criteria

- [x] Locate `UiEventHandler` in `libs/draxul-nvim/include/draxul/` or `src/`.
- [x] Add null-pointer assertions to `set_grid()`, `set_highlights()`, `set_options()` (assert the pointer is non-null at call time).
- [x] Add defensive null checks before dereferencing the stored pointers in hot paths (or assert non-null in `process_redraw()` entry point).
- [x] Add a `DRAXUL_ASSERT` or `DRAXUL_LOG_WARN` when a callback `std::function` field is invoked while empty (to surface forgetting to wire a callback during development, not to crash in release).
- [x] Document the lifetime contract in a comment above the class: "all pointers passed to `set_*` must outlive this object".
- [x] Do NOT refactor to a callback interface object in this item — that is a larger refactor. Scope this to safety assertions only.
- [x] Build and run `ctest`.

## Implementation Notes

- This is intentionally a minimal safety improvement, not a full redesign.
- The companion test item `06 uieventhandler-null-grid-crash -test` should be written after this fix to lock in the null-pointer defensive behaviour.

## Interdependencies

- **Blocks:** `06 uieventhandler-null-grid-crash -test` (write the test after the fix is in).
- No other blockers.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*

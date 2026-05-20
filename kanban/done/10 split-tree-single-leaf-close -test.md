# 10 split-tree-single-leaf-close -test

**Type:** test
**Priority:** 10
**Source:** Claude review (review-latest.claude.md)

## Problem

`HostManager` documents an invariant: calling `close_leaf()` when only one leaf exists should return `false` without freeing the root node. There is no dedicated unit test for the underlying `SplitTree` class to verify this invariant holds. If `SplitTree::close_leaf()` silently frees the last node, subsequent access will be a use-after-free.

## Acceptance Criteria

- [x] Locate `SplitTree` in `libs/draxul-host/` or `app/`.
- [x] Read its `close_leaf()` implementation and the existing `host_manager_tests.cpp`.
- [x] Add a unit test directly against `SplitTree` (not through `HostManager`) that:
  - [x] Creates a `SplitTree` with a single leaf.
  - [x] Calls `close_leaf()` on that leaf.
  - [x] Asserts the return value is `false`.
  - [x] Asserts `leaf_count() == 1` after the call (root is intact).
- [ ] Run under `ctest` and `mac-asan` to confirm no use-after-free.

## Implementation Notes

- This is a small, focused unit test. Should not require complex setup.
- Check if `SplitTree` already has a test file; if so, add to it. If not, create `tests/split_tree_tests.cpp`.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*

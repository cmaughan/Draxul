# 16 dividerhit-stable-handle -refactor

**Priority:** MEDIUM
**Type:** Refactor (replace void* divider node with stable DividerId)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`DividerHit::node` (`split_tree.h:32`) is a raw `void*` pointing to an internal `Node*`. This is an unsafe, untyped handle that:
1. Has no compile-time type safety.
2. Is dangling if the tree topology changes between `hit_test()` and `set_divider_ratio()` (addressed as a correctness bug in `04 dividerhit-dangling-void-ptr -bug`).
3. Uses a `// NOSONAR` suppression to silence static analysis.
4. Is visually inconsistent with `LeafId`, the stable typed handle already used for leaf nodes.

This refactor completes the cleanup started by the bug fix item `04`.

---

## Code Locations

- `app/split_tree.h:32` — `DividerHit::node void*`
- `app/split_tree.cpp` — `hit_test()` and `set_divider_ratio()` implementations
- `app/app.cpp` — the drag-handling call site

---

## Implementation Plan

- [x] Read `split_tree.h`, `split_tree.cpp`, and the relevant section of `app.cpp`.
- [x] Verify that `04 dividerhit-dangling-void-ptr -bug` has already introduced `DividerId` as an integer type and wired it into `set_divider_ratio()`.
- [x] Replace `void* node` in `DividerHit` with `DividerId divider_id`.
- [x] Remove the `// NOSONAR` comment (the suppression is now unnecessary).
- [x] Update any remaining cast sites.
- [x] Ensure `SplitTree` assigns a stable `DividerId` to each internal (non-leaf) node at creation and that the ID space is disjoint from `LeafId` (or uses a different type to avoid confusion at call sites).
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format`.

---

## Acceptance Criteria

- `DividerHit::node` is replaced by `DividerHit::divider_id` of type `DividerId`.
- No `void*` remains in `split_tree.h` for the divider path.
- `04 dividerhit-dangling-void-ptr -bug` acceptance criteria still pass.
- No regression in existing tests.

---

## Interdependencies

- **`04 dividerhit-dangling-void-ptr -bug`** — must be completed first (introduces `DividerId`). This item is the cleanup pass.
- No other blockers.

---

*claude-sonnet-4-6*

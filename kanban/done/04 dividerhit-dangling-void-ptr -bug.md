# 04 dividerhit-dangling-void-ptr -bug

**Priority:** MEDIUM
**Type:** Bug (potential dangling pointer on divider drag during topology change)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree::hit_test()` returns a `DividerHit` whose `node` field is a raw `void*` pointing directly to a `Node*` inside `SplitTree` (see `split_tree.h:32`). This pointer must be passed back to `SplitTree::set_divider_ratio()` within the same frame.

If the split tree topology changes between `hit_test()` and `set_divider_ratio()` — for example, if the user closes a pane while dragging a divider — the `void* node` pointer is dangling. The subsequent `set_divider_ratio()` call dereferences freed memory.

This also applies across frames: if the app holds the pointer from one frame and acts on it the next, any pane close in between invalidates it.

---

## Code Locations

- `libs/draxul-host/include/draxul/split_tree.h:32` — `DividerHit::node` definition
- `app/split_tree.cpp` — `hit_test()` and `set_divider_ratio()` implementations
- `app/app.cpp` — call site where `DividerHit` is stored and used for drag

---

## Implementation Plan

- [x] Read `split_tree.h`, `split_tree.cpp`, and the drag-handling path in `app.cpp` to understand the full hit-test → drag → ratio-set flow.
- [x] Introduce a `DividerId` type (a stable integer handle, similar to `LeafId`). Assign each internal node a unique `DividerId` at creation time. Store it in the `Node` struct.
- [x] Change `DividerHit::node` from `void*` to `DividerId`.
- [x] Update `set_divider_ratio()` to accept `DividerId` and look up the node in an internal map/iteration. If the `DividerId` is no longer valid (node was removed), return early (no-op + log debug).
- [x] Remove the `void*` and the `// NOSONAR` comment. No need for the unsafe cast.
- [x] The refactor item `16 dividerhit-stable-handle -refactor` covers the broader API cleanup; this item focuses on the correctness fix (validation on stale ID).
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- Dragging a divider while simultaneously closing a pane does not crash or dereference freed memory.
- `set_divider_ratio()` with a stale `DividerId` logs at `debug` level and returns without crashing.
- Normal divider drag continues to work.

---

## Interdependencies

- **`16 dividerhit-stable-handle -refactor`** — this bug item introduces `DividerId` minimally; the refactor item cleans up the broader API.
- No upstream blockers.

---

*claude-sonnet-4-6*

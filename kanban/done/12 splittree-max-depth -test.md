# 12 splittree-max-depth -test

**Priority:** LOW
**Type:** Test (stack overflow / crash safety under adversarial splitting)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree::recompute()` and `hit_test()` recurse through the binary tree. There is no test that verifies recursive depth does not cause a stack overflow when the tree is very deep (e.g., 100 splits of the same pane). There is also no test for what happens when a pane is smaller than 1 column / 1 row — the renderer could receive a zero-dimension viewport.

---

## Code Locations

- `app/split_tree.cpp` — `recompute()` and `hit_test()` recursive implementations
- `app/split_tree.h` — tree structure
- `tests/` — test file to add (likely near layout tests if they exist, or new `split_tree_tests.cpp`)

---

## Implementation Plan

- [x] Read `split_tree.cpp` and `split_tree.h` to understand the recursion structure and any existing depth limits.
- [x] Check if there are existing `SplitTree` tests; add new tests to the same file if so.
- [x] Write a test: programmatically split the same leaf 100 times (always split the first leaf). Assert:
  - `recompute()` completes without crashing.
  - `hit_test()` at an arbitrary pixel coordinate completes without crashing.
  - `for_each_leaf()` enumerates exactly 101 leaves.
- [x] Write a second test: split until panes are narrower than 1 pixel. Assert that `recompute()` either clamps sizes to a minimum (1 column / 1 row) or marks zero-size panes as not-renderable. The renderer should never receive a zero-dimension viewport.
- [x] If the implementation does not handle these cases safely, add a depth cap or minimum-size clamp and document it in `split_tree.h`.
- [x] Build and run tests.
- [x] Run `clang-format`.

---

## Acceptance Criteria

- 100-deep split does not crash or stack-overflow.
- Zero-dimension panes do not crash the renderer.
- `for_each_leaf()` enumerates the correct count.

---

## Interdependencies

- No upstream blockers. Self-contained.

---

*claude-sonnet-4-6*

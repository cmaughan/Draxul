# WI 118 — SplitTree O(n) Recursive Finds with `std::function` Allocations

**Type:** Bug / Performance  
**Severity:** Medium (latency degrades with many panes; compounds with other split operations)  
**Source:** Claude review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`app/split_tree.cpp` implements `find_leaf_node()`, `find_parent_of()`, and `find_divider_node()` as recursive tree walks, each allocating a `std::function<>` closure on every call. Several operations chain multiple such calls, making common paths effectively O(n²) in pane count.

From Claude's review: "Every `find_leaf_node()`, `find_parent_of()`, and `find_divider_node()` call allocates a `std::function<>` and recursively walks the entire tree. Several operations chain multiple find calls making common paths O(n²). The `std::function` closure allocations are unnecessary given the small tree size."

While the tree is small in typical use, this degrades measurably when stress-testing splits (related to existing WI 12 `splittree-max-depth`), and the `std::function` allocations add pressure to the heap on every pane interaction.

Note: `app/split_tree.cpp` has an unstaged modification in the current working tree — check if this overlaps with in-progress work before starting.

---

## Investigation Steps

- [x] Check current git status on `app/split_tree.cpp` — is there in-progress work?
- [x] Profile `find_leaf_node`, `find_parent_of`, `find_divider_node` — confirm `std::function` heap allocation per call
- [x] Enumerate callers — which operations chain multiple finds?
- [ ] Measure allocation count for a 10-pane split with a flame graph or ASan allocation tracing

---

## Fix Strategy

1. Replace `std::function<>` predicate callbacks with templated lambda parameters or function-pointer overloads. Eliminates heap allocation entirely.
2. For chained finds (e.g. find parent then use to find sibling), combine into a single tree pass that returns both results.
3. Consider caching parent pointers in each node if the tree is frequently queried.

```cpp
// Before
template<typename Fn>
Node* find_leaf_node(std::function<bool(const LeafNode&)> pred);

// After
template<typename Pred>
Node* find_leaf_node(Pred&& pred);
```

---

## Acceptance Criteria

- [x] `find_leaf_node`, `find_parent_of`, `find_divider_node` no longer use per-call `std::function` allocations (now plain static recursive helpers).
- [ ] No chained double-traversal for operations that can be combined. (Out of scope for this minimal fix; left for follow-up.)
- [x] Existing split tree tests pass.
- [x] No regression in `splittree-max-depth` or `splittree-oob-placement` test scenarios.

## Status

Replaced the `std::function<>`-wrapped recursive lambdas inside `find_leaf_node`,
`find_divider_node`, `find_parent_of`, and `for_each_divider` (in
`app/split_tree.cpp`) with plain static recursive helper functions
(`find_leaf_impl`, `find_divider_impl`, `find_parent_impl`, `visit_dividers`)
declared on `SplitTree` in `app/split_tree.h`. The non-const overloads now
forward to a single const implementation via `const_cast`, eliminating both the
per-call heap allocation of the closure and the duplicated recursive bodies.

The signatures of the public/private member functions are unchanged, so no
callers needed updating. Build (`cmake --build build --target draxul
draxul-tests`) and the full `draxul-tests` ctest suite pass (1010 cases, 0
failures). Did not add new tests — existing `split_tree_tests.cpp` already
exercises the relevant operations and the change is a pure internal
refactor with identical observable behaviour.

---

## Interdependencies

- **WI 29** (pane-drag-reorder) and **WI 44** (pane-zoom) both traverse the split tree frequently — this fix benefits them.
- Check against any in-progress `split_tree.cpp` changes in the working tree.

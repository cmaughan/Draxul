# 16 splittree-const-correctness -refactor

**Priority:** LOW
**Type:** Refactor (correctness, const safety)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree::find_leaf_node()` is declared `const` but returns `Node*` (non-const pointer). This silently discards the const qualifier of `this`. The implementation recurses over `root_.get()` but returns a mutable pointer, meaning a caller receiving a `const SplitTree&` can indirectly mutate the tree through the returned pointer. This is technically undefined behavior if the tree was declared `const`. It also misleads readers about the const contract.

---

## Implementation Plan

- [ ] Find `SplitTree::find_leaf_node()` in the source (search `libs/` or `app/` for `SplitTree`).
- [ ] Evaluate the fix strategy:
  - **Preferred:** Provide two overloads — a `const` version returning `const Node*` and a non-const version returning `Node*`. This is the standard C++ pattern for mutable/const access.
  - **Alternative:** Remove the `const` qualifier from the existing method if all callers actually need a mutable `Node*`.
- [ ] Check all call sites to determine which overload they need.
- [ ] Implement the chosen fix and update callers as needed.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format`.

---

## Acceptance

- A `const SplitTree&` cannot be used to obtain a mutable `Node*` through `find_leaf_node()`.
- All call sites compile cleanly.
- All tests pass.

---

## Interdependencies

- No upstream blockers; self-contained.
- A compile-time regression test (see the "SplitTree const-correctness regression" suggestion from Claude's review) can be added as a bonus in the same pass.

---

*claude-sonnet-4-6*

# Bug: Grid Index Arithmetic Overflow UB

**Type:** bug
**Priority:** 1
**Source:** Claude review

## Problem

In `libs/draxul-grid/src/grid.cpp`, cell index computation uses the pattern:

```cpp
cells_[row * cols_ + col]
```

`row` and `cols_` are likely `int` or a signed type. If both are large (e.g. a pathological Neovim resize message sends `rows=100000, cols=100000`), the multiplication overflows signed integer arithmetic — which is undefined behaviour in C++.

In practice, modern terminals do not send such sizes, but:
- The UB could theoretically be triggered by a malformed RPC message.
- Any future use of `Grid` in a test context with large dimensions would hit this.
- ASan/UBSan flags signed overflow and this will surface as a sanitiser failure.

Key file: `libs/draxul-grid/src/grid.cpp`

## Investigation steps

- [x] Read `grid.cpp` and identify every site where `row * cols_` (or equivalent) is used for index computation.
- [x] Check the types of `row`, `col`, `cols_`, `rows_` — are they `int`, `uint32_t`, `size_t`?
- [x] Check whether `Grid::resize()` validates `rows * cols` before allocating.
- [x] Check `Grid::set_cell()` and `Grid::scroll()` for the same pattern.

## Fix strategy

- [x] Change the index computation to use `size_t` arithmetic:
  ```cpp
  cells_[static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col)]
  ```
- [x] In `Grid::resize()`, add a guard:
  ```cpp
  DRAXUL_ASSERT(rows <= kMaxGridDim && cols <= kMaxGridDim);
  ```
  where `kMaxGridDim` is a reasonable constant (e.g. 10000).
- [x] Ensure existing callers that pass `int` values are not broken by the type change; add `static_cast` at call sites as needed.

## Acceptance criteria

- [x] No signed-integer overflow UB when grid is indexed with any valid terminal size.
- [x] `cmake --preset mac-asan` build passes the full test suite with no UBSan overflow errors.
- [x] `Grid::resize()` rejects unreasonably large dimensions with an assertion.

## Interdependencies

- `05 grid-oob-writes -test`: write OOB boundary tests after this fix; they should confirm the guard fires correctly.
- `17 grid-scroll-ops-split -refactor`: the scroll refactor touches the same arithmetic; sequence overflow fix first.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*

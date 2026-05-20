# renderer_state / grid: latent signed overflow in index computation

**Severity:** MEDIUM  
**Files:** `libs/draxul-renderer/src/renderer_state.cpp:242,259`; `libs/draxul-grid/src/grid.cpp:481`  
**Source:** review-bugs-consensus BUG-08 (claude)

## Bug Description

Two index calculations use signed arithmetic without local guards:

**`renderer_state.cpp:242` and `:259`:**
```cpp
int idx = cursor_row_ * grid_cols_ + cursor_col_;
```
`cursor_row_` and `grid_cols_` are both `int`. The maximum product with `kMaxGridDim = 10000` is 100 M, safely within `INT_MAX`. But the multiplication is signed; if either dimension ever escapes validation, the product overflows as UB and the subsequent `gpu_cells_[(size_t)idx]` index is unpredictable.

**`grid.cpp:481`:**
```cpp
if (index < 0 || index >= (int)cells_.size())
```
The narrowing cast from `size_t` to `int` is implementation-defined if `cells_.size() > INT_MAX`. Current `kMaxGridDim` prevents this, but the cast provides no local safety net.

**Trigger:** Currently unreachable due to `kMaxGridDim`. Becomes a real OOB write if `kMaxGridDim` is raised above ~46 340 (√INT_MAX) or if any code path bypasses validation.

## Investigation

- [ ] Confirm there are no other sites in `renderer_state.cpp` that compute `row * cols` with signed types
- [ ] Check if `grid.cpp` has other locations with the same `(int)cells_.size()` pattern

## Fix Strategy

- [ ] `renderer_state.cpp`: cast to `size_t` before multiply:
  ```cpp
  const size_t idx = static_cast<size_t>(cursor_row_) * static_cast<size_t>(grid_cols_)
                   + static_cast<size_t>(cursor_col_);
  ```
- [ ] `grid.cpp`: use unsigned comparison:
  ```cpp
  if (index < 0 || static_cast<size_t>(index) >= cells_.size())
  ```
- [ ] Search for similar patterns (`(int)something.size()`) elsewhere in grid and renderer code and fix consistently

## Acceptance Criteria

- [ ] No `(int)` cast on any `size()` return value in the patched files
- [ ] No signed `row * cols` multiplication in `renderer_state.cpp`
- [ ] `draxul-tests` passes under `mac-asan` and `mac-tsan` presets
- [ ] Compiler warnings for implicit narrowing (if enabled) do not fire on the changed lines

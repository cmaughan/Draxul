# 14 scrollback-ring-buffer — Refactor

## Summary

`TerminalHostBase::newline()` in `libs/draxul-host/src/terminal_host_base.cpp` handles scrolling the terminal grid up by one line and, when the scrollback capacity is exceeded, discards the oldest row. The current implementation appends a new `std::vector<Cell>` to the scrollback buffer for each scrolled-off row:

```cpp
scrollback_.push_back(std::vector<Cell>(term_cols_));  // one heap alloc per newline
```

When running `cat /usr/share/dict/words` (235,000 lines) in a small terminal, this creates 235,000 heap allocations. Under heavy scrolling, malloc pressure is measurable.

A ring buffer of pre-allocated rows eliminates the hot-path per-row allocation. The ring buffer allocates `kCapacity` rows of `term_cols_` cells once at initialization; `newline()` advances a write-head index.

## Steps

- [x] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` in full. Find `newline()` and all code that reads from or writes to `scrollback_`.
- [x] 2. Read `libs/draxul-host/include/draxul/` for any `ScrollbackBuffer` class definition or `scrollback_` member declaration.
- [x] 3. Read `libs/draxul-app-support/include/draxul/` and `libs/draxul-host/include/draxul/` for the `kScrollbackCapacity` constant (or wherever the scrollback limit is defined).
- [x] 4. Understand the access patterns for scrollback:
  - Who reads from `scrollback_` (e.g., scrollback-viewport rendering in grid_host_base)?
  - What order does the read access expect (oldest-first? newest-first?)?
  - Is random access needed, or only sequential iteration?
- [x] 5. Design the ring buffer. A minimal implementation:
  ```cpp
  class ScrollbackRingBuffer {
  public:
      explicit ScrollbackRingBuffer(int capacity, int cols);
      void push(std::span<const Cell> row);       // advances write head, overwrites oldest
      int size() const;                            // number of valid rows (up to capacity)
      std::span<const Cell> operator[](int i) const;  // index 0 = oldest, size()-1 = newest
  private:
      std::vector<Cell> storage_;  // capacity * cols cells, pre-allocated once
      int capacity_;
      int cols_;
      int write_head_ = 0;
      int count_ = 0;
  };
  ```
  Alternatively, use `std::deque<std::array<Cell, kMaxCols>>` with a max-cols compile constant, but the flat vector approach avoids all fragmentation.
- [x] 6. Implement ring buffer storage directly inside `ScrollbackBuffer` (flat `std::vector<Cell> storage_` with `write_head_` and `count_` fields). Two-phase push API: `next_write_slot()` + `commit_push()` eliminates all temporary allocation.
- [x] 7. Replace `std::vector<std::vector<Cell>> buf_` in `ScrollbackBuffer` with flat ring-buffer fields.
- [x] 8. Update `newline()` to use `next_write_slot()` / `commit_push()` instead of building a temporary `std::vector<Cell>`.
- [x] 9. Update all read sites (`update_display()`) to use the private `row(int i)` accessor returning `std::span<const Cell>`.
- [x] 10. Handle resize: added `resize(int cols)` to `ScrollbackBuffer`; called from `on_viewport_changed()` whenever `new_cols != grid_cols()`, discarding scrollback.
- [x] 11. Build: `cmake --build build --target draxul draxul-tests`. Confirm no compile errors.
- [x] 12. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass — this is a behavioral no-op refactor.
- [x] 13. Manual smoke test: `py do.py smoke`. Verify scrollback still works (scroll up in the running neovim, or `cat` a long file and scroll back).
- [x] 14. Run clang-format on all touched files.
- [x] 15. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `TerminalHostBase::newline()` no longer calls `std::vector<Cell>` constructor on each scroll-off.
- `ScrollbackRingBuffer` pre-allocates storage once at construction.
- All scrollback read paths produce the same ordered content as before.
- All tests pass.
- Smoke test confirms scrollback is still usable.

*Authored by: claude-sonnet-4-6*

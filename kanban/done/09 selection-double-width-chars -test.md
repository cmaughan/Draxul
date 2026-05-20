# 09 selection-double-width-chars — Test

## Summary

Wide characters (e.g., CJK ideographs such as `你`, `中`, `日`) occupy 2 columns in the terminal grid. The first column holds the character's glyph; the second column is a "continuation" cell that is marked wide/empty. `GridHostBase::extract_text()` must handle selection ranges that span wide characters without:

- Double-encoding the character (appending it once for the primary cell and once for the continuation cell).
- Half-encoding the character (including only the continuation cell).
- Skipping the character entirely when the selection starts at the continuation cell.

This is currently untested. Incorrect handling produces garbled clipboard text.

## Steps

- [ ] 1. Read `libs/draxul-host/src/grid_host_base.cpp` to find `extract_text()` and the wide-character handling logic. Note how the continuation cell is identified (a flag, a zero codepoint, a special width field, etc.).
- [ ] 2. Read `libs/draxul-grid/include/draxul/grid.h` (or `libs/draxul-types/include/draxul/types.h`) to find the `Cell` struct and understand the `width` or `is_wide_continuation` field.
- [ ] 3. Read `tests/terminal_vt_tests.cpp` or any existing selection test to understand how grid rows are constructed in tests and how `extract_text()` is called.
- [ ] 4. Read `tests/CMakeLists.txt` and `tests/test_main.cpp` for registration patterns.
- [ ] 5. Create `tests/selection_wide_char_tests.cpp` (or add to an existing selection test file).

  **Test setup helper:** Write a helper function that builds a `Grid` row from a vector of `(codepoint, width)` pairs, automatically filling continuation cells for 2-wide characters.

  **Test 1: Select a range that contains a wide character in the middle**
  - Build row: `[A(1), 你(2+cont), B(1)]` → columns 0, 1, 2, 3 (A at 0, 你 at 1, continuation at 2, B at 3).
  - Select columns 0..3 (entire row).
  - Call `extract_text()`.
  - Assert: result is `"A你B"` (UTF-8, wide char appears exactly once).

  **Test 2: Select starting at the continuation cell of a wide character**
  - Same row: `[A(1), 你(2+cont), B(1)]`.
  - Select columns 2..3 (starting at the continuation cell of 你).
  - Call `extract_text()`.
  - Assert: result is either `"你B"` (wide char included — selection extended left to include the primary cell) OR `"B"` (wide char excluded — selection starts after it). Document which behavior is implemented and verify it is consistent.

  **Test 3: Select ending at the primary cell of a wide character (not including the continuation)**
  - Same row.
  - Select columns 0..1 (A and the primary cell of 你, but not the continuation at column 2).
  - Call `extract_text()`.
  - Assert: result is either `"A你"` (wide char included — selection extended right) OR `"A"` (wide char excluded). Document and verify consistency.

  **Test 4: Multiple consecutive wide characters**
  - Build row: `[你(2+cont), 好(2+cont), !(1)]` → columns 0,1,2,3,4.
  - Select columns 0..4 (entire row).
  - Assert: result is `"你好!"` (each wide char appears exactly once).

  **Test 5: Selection of only the continuation cell of a wide character**
  - Select exactly column 2 (continuation of 你 in the row from test 1).
  - Assert: result is either `"你"` or `""` but NOT a partial/garbled encoding.

- [ ] 6. Register the new test file in `tests/CMakeLists.txt`.
- [ ] 7. Register test cases in `tests/test_main.cpp` if manual registration is needed.
- [ ] 8. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [ ] 9. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [ ] 10. If any test reveals a real bug in `extract_text()`, fix the bug as part of this work item and note the fix in the checkboxes below:
  - [ ] Bug fix (if needed): _______________
- [ ] 11. Run clang-format on all touched files.
- [ ] 12. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- At least 4 test cases covering wide character selection boundaries are present and passing.
- No wide character is double-encoded or partially-encoded in the extracted text.
- Selection starting at a continuation cell produces a defined, documented, consistent result.
- All existing tests continue to pass.

*Authored by: claude-sonnet-4-6*

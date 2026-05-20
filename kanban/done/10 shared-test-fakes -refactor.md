# 10 shared-test-fakes — Refactor

## Summary

`FakeWindow` and `FakeTermRenderer` (or similar fake renderer types) are independently declared in anonymous namespaces in multiple test files:

- `tests/terminal_vt_tests.cpp`
- `tests/terminal_mouse_tests.cpp`
- Possibly others

Each version accumulates slightly different capabilities (some have `last_cursor_shape()`, some don't; some track `set_cell_size()` calls, some don't). When a new test needs a capability that only one version has, the author either duplicates code or silently omits the assertion.

`tests/support/test_support.h` exists but does not provide these shared fakes.

This refactor centralizes the canonical implementations so all test files pull from one source of truth.

## Steps

- [x] 1. Read `tests/terminal_vt_tests.cpp` in full, especially the anonymous-namespace `FakeWindow` and fake renderer declarations. Note every method and tracked field.
- [x] 2. Read `tests/terminal_mouse_tests.cpp` in full for its own fake declarations. Note all differences from the first file.
- [x] 3. Search for any other test files with fake declarations: found `selection_truncation_tests.cpp` also had identical `FakeWindow`/`FakeTermRenderer`.
- [x] 4. Read `tests/support/test_support.h` in full to understand what helpers already exist there.
- [x] 5. Create `tests/support/fake_window.h` with `class FakeWindow : public IWindow`, union of all capabilities, `reset()` method, `#pragma once`.
- [x] 6. Create `tests/support/fake_renderer.h` with `class FakeTermRenderer : public IRenderer`, union of all capabilities, `reset()` method, `#pragma once`.
- [x] 7. Updated `tests/terminal_vt_tests.cpp`: removed anonymous-namespace fakes, added shared header includes.
- [x] 8. Updated `tests/terminal_mouse_tests.cpp` similarly.
- [x] 9. Updated `tests/selection_truncation_tests.cpp` similarly.
- [x] 10. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [x] 11. Run: `ctest --test-dir build -R draxul-tests`. ALL existing tests pass.
- [x] 12. Run clang-format on all touched files including the new `tests/support/` headers.
- [x] 13. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `tests/support/fake_window.h` and `tests/support/fake_renderer.h` exist with the union of all capabilities from the per-file fakes.
- No anonymous-namespace fake declarations remain in `terminal_vt_tests.cpp` or `terminal_mouse_tests.cpp`.
- All existing tests pass without modification to their assertion logic.
- The shared fakes are used by at least 2 test files each.

*Authored by: claude-sonnet-4-6*

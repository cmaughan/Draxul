# 06 decsc-decrc-save-restore — Test

## Summary

`TerminalHostBase::handle_esc()` implements ESC 7 (DECSC — save cursor position and attributes) and ESC 8 (DECRC — restore cursor position and attributes). These are used by terminal programs that need to park the cursor, print a status line, then restore the cursor to its original position (e.g., vim's status bar rendering, prompt systems).

The implementation exists but has zero dedicated test coverage. Regressions in this path would be silent.

## Steps

- [ ] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` to find `handle_esc()` and locate the ESC 7 / ESC 8 handling. Note exactly what state is saved and restored (cursor row, cursor col, SGR attributes, character set designation, origin mode, etc.).
- [ ] 2. Read `tests/terminal_vt_tests.cpp` to understand the existing test structure, how the terminal host is instantiated, and how VT sequences are fed to it.
- [ ] 3. Check `tests/support/test_support.h` for any existing terminal test helpers.
- [ ] 4. Add the following test cases to `tests/terminal_vt_tests.cpp` (or a new file `tests/decsc_decrc_tests.cpp` if the existing file is already large):

  **Test 1: Basic save and restore of cursor position**
  - Set cursor to row=3, col=5 via CSI H (cursor position).
  - Feed `ESC 7` (DECSC).
  - Move cursor to row=0, col=0.
  - Feed `ESC 8` (DECRC).
  - Assert: cursor is at row=3, col=5.

  **Test 2: Restore after multiple moves**
  - Set cursor to row=2, col=7.
  - Feed `ESC 7`.
  - Move cursor around several times (row=0 col=0, row=5 col=10, row=1 col=1).
  - Feed `ESC 8`.
  - Assert: cursor is at row=2, col=7 (the saved position, not any intermediate position).

  **Test 3: Save and restore of SGR attributes**
  - Set an SGR attribute (e.g., bold, `\x1B[1m`, or a foreground color `\x1B[31m`).
  - Move cursor to row=1, col=1.
  - Feed `ESC 7`.
  - Reset attributes (`\x1B[0m`) and move cursor to row=0, col=0.
  - Feed `ESC 8`.
  - Assert: cursor is at row=1, col=1 AND the restored active attribute is bold/red (not reset).
  - (Only assert attributes if `handle_esc()` actually saves/restores them — check step 1.)

  **Test 4: DECRC before any DECSC (no saved state)**
  - Feed `ESC 8` without having fed `ESC 7` first.
  - Assert: no crash; cursor position is either unchanged or reset to (0, 0) per the terminal spec (document whichever behavior is implemented).

  **Test 5: Nested saves (second ESC 7 overwrites the first)**
  - Save at row=1, col=1 via `ESC 7`.
  - Move to row=3, col=3.
  - Save again via `ESC 7`.
  - Move to row=5, col=5.
  - Restore via `ESC 8`.
  - Assert: cursor is at row=3, col=3 (last saved, not the first).

- [ ] 5. If a new test file is created, register it in `tests/CMakeLists.txt` and `tests/test_main.cpp`.
- [ ] 6. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [ ] 7. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [ ] 8. Run clang-format on all touched files.
- [ ] 9. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- At least 4 test cases covering DECSC/DECRC are present and passing.
- Both cursor-position and attribute save/restore are tested (if the implementation saves attributes).
- The edge case of DECRC before DECSC is tested and documented.
- All existing tests continue to pass.

*Authored by: claude-sonnet-4-6*

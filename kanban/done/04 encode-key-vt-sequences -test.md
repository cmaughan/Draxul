# 04 encode-key-vt-sequences — Test

## Summary

`TerminalHostBase::encode_key()` in `libs/draxul-host/` translates SDL key events to VT escape sequences that are sent to Neovim via stdin. There are currently zero unit tests asserting the correct output bytes for any key. This item adds a test file that:

1. Documents and guards the current correct behavior for all keys that are already implemented.
2. Documents known gaps (F-keys, Alt+letter) as explicit stubs/TODOs so that when feature item 18 implements them, the tests can be activated.

**This test item is a prerequisite companion for item 18 (terminal-keyboard-completeness).** Write these tests first, then implement the missing keys in item 18 so the tests pass.

## Steps

- [x] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` in full to find `encode_key()` (or the equivalent key encoding method). Note the full set of keys currently handled.
- [x] 2. Read `libs/draxul-host/include/draxul/` header(s) to understand `TerminalHostBase`'s public API and whether `encode_key()` is directly callable from a test, or whether it requires a concrete subclass.
- [x] 3. If `encode_key()` requires a concrete subclass or side-effecting context, create a minimal test subclass (`class TestableTerminalHost : public TerminalHostBase { /* minimal overrides */ }`) in the test file.
- [x] 4. Read `tests/CMakeLists.txt` and `tests/test_main.cpp` for test registration patterns.
- [x] 5. Create `tests/encode_key_tests.cpp`. Added test cases for all keys including F1-F12, Alt+letter, DECCKM arrows (activated as part of item 18 implementation).
- [x] 6. Register the new test file in `tests/CMakeLists.txt`.
- [x] 7. Register test cases in `tests/test_main.cpp`.
- [x] 8. Build: `cmake --build build --target draxul-tests`. No compile errors.
- [x] 9. Run: `ctest --test-dir build -R draxul-tests`. All tests pass.
- [x] 10. Run clang-format on the new test file.
- [x] 11. Mark this work item complete and move to `plans/work-items-complete/`. (Implemented together with item 18.)

## Acceptance Criteria

- `tests/encode_key_tests.cpp` exists and covers all currently-implemented keys with exact byte assertions.
- F-key and Alt+letter expected sequences are documented in the test file (as comments or disabled assertions).
- All passing tests continue to pass after this change.

*Authored by: claude-sonnet-4-6*

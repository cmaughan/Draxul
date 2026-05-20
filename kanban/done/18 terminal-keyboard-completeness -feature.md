# 18 terminal-keyboard-completeness — Feature

## Summary

`TerminalHostBase::encode_key()` in `libs/draxul-host/src/terminal_host_base.cpp` is severely incomplete for standard terminal emulation. Missing functionality:

- **F1-F12:** Common in htop, mutt, tmux, vim function-key bindings.
- **Alt+letter:** Used by readline (Alt+b = move word back, Alt+f = move word forward), emacs, tmux prefix keys.
- **DECCKM (application cursor mode):** When enabled by `\x1B[?1h`, arrow keys emit `\x1BOA/B/C/D` (SS3 sequences) instead of `\x1B[A/B/C/D`. This is required by vim, less, and most full-screen terminal programs that use smkx/rmkx.

**Prerequisite:** Item 04 (encode-key-vt-sequences test) should be written first or alongside. Write the test cases for each key, confirm the test for currently-implemented keys passes, then implement the missing keys so the stub tests activate.

## Standard VT Sequences to Implement

### F-keys (xterm standard)
| Key | Sequence |
|---|---|
| F1 | `\x1BOP` |
| F2 | `\x1BOQ` |
| F3 | `\x1BOR` |
| F4 | `\x1BOS` |
| F5 | `\x1B[15~` |
| F6 | `\x1B[17~` |
| F7 | `\x1B[18~` |
| F8 | `\x1B[19~` |
| F9 | `\x1B[20~` |
| F10 | `\x1B[21~` |
| F11 | `\x1B[23~` |
| F12 | `\x1B[24~` |

Note: F6–F12 use `17~`–`24~` (skipping 16 and 22) per the VT220 standard.

### Alt+letter
When `mod & SDL_KMOD_ALT` (or `SDL_KMOD_LALT | SDL_KMOD_RALT`), prefix the normal character encoding with `\x1B`:
- Alt+a → `\x1Ba`, Alt+z → `\x1Bz`
- Alt+A (with shift) → `\x1BA`
- Alt+0 through Alt+9 → `\x1B0` through `\x1B9`
- This applies to all printable ASCII characters.

### DECCKM (application cursor mode)
- State variable: `bool cursor_app_mode_ = false;` in `TerminalHostBase`.
- Activated by private DEC mode `\x1B[?1h` (SM with `?1` parameter).
- Deactivated by `\x1B[?1l` (RM with `?1` parameter).
- This mode switch is handled in `handle_csi()` under the `h`/`l` DEC private mode case (`?h`/`?l`).
- When `cursor_app_mode_` is true, arrow keys emit:
  - Up → `\x1BOA`
  - Down → `\x1BOB`
  - Right → `\x1BOC`
  - Left → `\x1BOD`
- When `cursor_app_mode_` is false (default), arrows emit `\x1B[A/B/C/D` (normal mode).

## Steps

- [x] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` in full. Find `encode_key()` and understand the current SDL keycode → sequence mapping structure.
- [x] 2. Read `libs/draxul-host/include/draxul/terminal_host_base.h` (or the class header) to see all current member variables and understand where to add `cursor_app_mode_`.
- [x] 3. Confirm item 04 (encode-key-vt-sequences test) exists. If not, write the test stubs first (at minimum document the expected byte sequences in comments in the test file) before implementing here.
- [x] 4. Add `bool cursor_app_mode_ = false;` to `TerminalHostBase` private members in both the public header and the private src header.
- [x] 5. In `csi_mode()`, add handling for DEC private mode `?1` (DECCKM):
  ```cpp
  case 1:  // DECCKM — application cursor keys
      cursor_app_mode_ = enable;
      break;
  ```
- [x] 6. In `encode_key()`, update arrow key cases to be cursor-mode-aware (SS3 vs CSI).
- [x] 7. Add F1-F12 cases in `encode_key()`.
- [x] 8. Add Alt+printable-ASCII handling in `encode_key()`.
- [x] 9. Create `tests/encode_key_tests.cpp` with tests for F1-F12, DECCKM arrows, and Alt+letter/digit. Register in CMakeLists.txt and test_main.cpp.
- [x] 10. Build: `cmake --build build --target draxul draxul-tests` — clean.
- [x] 11. Run: `ctest --test-dir build -R draxul-tests` — all pass.
- [x] 12. Run clang-format on all touched source files.
- [ ] 13. Manual smoke test with htop (F1/F2/F10, arrow keys).
- [ ] 14. Manual smoke test with readline Alt+b / Alt+f word movement.

## Acceptance Criteria

- F1-F12 emit the standard xterm sequences documented above.
- Alt+letter emits `\x1B + char`.
- Arrow keys emit DECCKM application sequences when `\x1B[?1h` has been received.
- Arrow keys emit normal CSI sequences after `\x1B[?1l`.
- All tests from item 04 pass with the stub assertions activated.
- htop F-key navigation works in manual testing.
- readline Alt+b / Alt+f word movement works in manual testing.

*Authored by: claude-sonnet-4-6*

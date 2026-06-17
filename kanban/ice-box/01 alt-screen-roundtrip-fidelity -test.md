# 01 alt-screen-resize-roundtrip-fidelity -test

**Type:** test
**Priority:** Medium

## Current State

Basic alternate-screen save/restore is already covered in `tests/terminal_vt_tests.cpp` by `terminal: alternate screen exit restores main screen content`. Scrollback exclusion for alt-screen output is also covered in `tests/scrollback_overflow_tests.cpp`.

The remaining gap is narrower: resize while inside the alternate screen should preserve and correctly resize the saved main-screen snapshot.

## Goal

Add a focused regression test for main-screen restoration after entering alt screen, resizing, writing unrelated alt-screen content, and exiting alt screen.

## Tasks

- [ ] Use the existing `VtTerminalSetup` / terminal host test style in `tests/terminal_vt_tests.cpp`.
- [ ] Populate multiple main-screen rows with recognizable content.
- [ ] Enter alt screen with `ESC[?1049h`.
- [ ] Resize the host while alt screen is active.
- [ ] Write different alt-screen content after the resize.
- [ ] Exit alt screen with `ESC[?1049l`.
- [ ] Assert restored main-screen cells match the expected resized snapshot.
- [ ] Include cursor-position expectations if the terminal host exposes them clearly.

## Acceptance Criteria

- The test fails if resize inside alt screen clobbers saved main-screen content.
- The test does not spawn Neovim or require a real renderer.
- Existing alt-screen and scrollback tests still pass.

# 09 mouse-modifier-fidelity -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Gemini (validates 03-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

Mouse modifier state is currently sampled from global SDL state rather than the event itself. There is no test that constructs synthetic SDL mouse events with specific modifier flags and asserts the translated Neovim mouse string carries the correct modifiers.

---

## Implementation Plan

- [x] Read `libs/draxul-window/src/sdl_event_translator.cpp` and the corresponding header to understand the translation API.
- [x] Find or create a test-seam to feed synthetic `SDL_MouseButtonEvent`, `SDL_MouseWheelEvent`, `SDL_MouseMotionEvent` into the translator without a real SDL window.
  - Used `SDL_SetModState()` to inject modifier state since SDL3 mouse events carry no `.mod` field.
  - Added `libs/draxul-window/src` to test include path; linked `draxul-window` to test target.
- [x] Write tests in `tests/mouse_modifier_tests.cpp`:
  - Button events: no-modifier baseline, Ctrl+Shift, Alt-only, release, wrong-type.
  - Wheel events: no-modifier baseline, Alt-only, Ctrl+Alt, wrong-type.
  - Motion events: no-modifier baseline, Shift-only, wrong-type.
- [x] Add to `tests/CMakeLists.txt`.
- [x] Run ctest — all 12 new tests pass.

---

## Acceptance

- Synthetic mouse events with modifier flags produce the correct Neovim mouse notation.
- No global SDL state is consulted during the test (event struct values are authoritative).

---

## Interdependencies

- Best written after **03-bug** fix (global-SDL-state removal). Can be TDD — test will fail before fix if global state is inconsistent in tests.

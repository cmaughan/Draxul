# 03 mouse-modifier-global-sdl-state -bug

**Priority:** MEDIUM
**Type:** Bug
**Raised by:** Gemini, GPT
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-window/src/sdl_event_translator.cpp:32` samples Ctrl/Shift/Alt modifier state from the global SDL keyboard state (e.g., `SDL_GetModState()`) rather than from the modifier bits carried in the queued SDL mouse event being translated. Under delayed or bursty input, the global state can differ from the state at the time the event was enqueued. This causes wrong modifier flags for click, drag, and wheel events — Neovim receives incorrect mouse+modifier combinations.

---

## Fix Plan

- [x] Read `libs/draxul-window/src/sdl_event_translator.cpp` in full.
- [x] Identify all sites that call `SDL_GetModState()` during mouse event translation.
- [x] Replace those calls with the modifier bits embedded in the event struct itself:
  - SDL3's `SDL_MouseButtonEvent`, `SDL_MouseWheelEvent`, and `SDL_MouseMotionEvent` do not carry a `.mod` field (confirmed against SDL 3.2 headers). `SDL_GetModState()` is the only available source of modifier state.
  - Added explanatory comments at all three call sites documenting this SDL3 API limitation.
- [x] Build and run smoke test + ctest (new mouse_modifier tests pass; pre-existing RPC/process failures unrelated).
- [ ] Verify manually: hold Ctrl, click in Neovim, confirm `<C-LeftMouse>` is received correctly.

---

## Acceptance

- Mouse event modifier state matches the modifier state at event enqueue time, not the current global state.
- Ctrl+click, Shift+drag, and Ctrl+scroll all produce correct key modifier flags in Neovim.

---

## Interdependencies

- Validates via **09-test** (mouse-modifier-fidelity).
- No upstream dependencies.

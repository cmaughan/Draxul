# 29 IME Composition Visibility

## Why This Exists

`App::on_text_editing()` is currently a no-op. When a user types with an IME (Input Method Editor —
used for CJK, kana, emoji, etc.), SDL fires `SDL_EVENT_TEXT_EDITING` with the in-progress composition
string. Draxul discards it entirely. The user sees nothing while they are composing a character;
their input appears to freeze until they commit a codepoint.

This is a significant UX problem for CJK users, who spend the majority of their input time in
the pre-edit (composition) phase.

**Source:** `app/app.cpp` — `on_text_editing()` callback, `SDL_EVENT_TEXT_EDITING`.
**Raised by:** Claude, GPT, Gemini (all three identify this as a user-facing gap).

## Goal

Display the IME pre-edit (composition) string at the cursor position while composition is in progress:
1. On `SDL_EVENT_TEXT_EDITING`, render the partial composition string as an overlay near the cursor.
2. On `SDL_EVENT_TEXT_INPUT` (commit), dismiss the overlay and send the committed text to Neovim normally.
3. On `SDL_EVENT_TEXT_EDITING` with empty string (cancel), dismiss the overlay.

## Implementation Plan

- [ ] Read `app/app.cpp` to understand how `on_text_editing` and `on_text_input` are wired.
- [ ] Read `libs/draxul-renderer/include/draxul/renderer.h` to understand what overlay drawing primitives are available (if any).
- [ ] Design the composition display:
  - **Option A (ImGui overlay):** Render the pre-edit string in an ImGui tooltip or floating window near the cursor. This requires no renderer changes.
  - **Option B (renderer overlay cell):** Insert a temporary "composition" cell into the grid at the cursor position. Simpler but may flicker on rapid edits.
  - Option A is preferred — it reuses the existing ImGui integration and does not pollute the grid.
- [ ] Implement the pre-edit string capture and overlay display.
- [ ] Call `SDL_StartTextInput` / `SDL_StopTextInput` at the correct points (SDL3 requires explicit start/stop).
- [ ] Test manually on macOS with a CJK input method (Pinyin or Kana).
- [ ] Run `ctest --test-dir build`.

## Notes

This feature is related to the platform integration icebox item 07. If 07 is thawed,
coordinate with it rather than duplicating IME work.

## Sub-Agent Split

Single agent. Most changes in `app/app.cpp` and `libs/draxul-ui/` (ImGui overlay).

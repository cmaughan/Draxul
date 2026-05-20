# 27 Configurable GUI Keybindings

## Why This Exists

GUI-specific actions in Draxul are hardcoded in `App::wire_window_callbacks()`:
- `F12` toggles the debug panel
- `Ctrl+Shift+C/V` for clipboard copy/paste
- `Ctrl+=/-/0` for font size changes

There is no remapping mechanism. Users on non-QWERTY layouts, or those with conflicting Neovim
bindings, cannot change these shortcuts. Adding a new GUI action currently requires editing C++.

**Source:** `app/app.cpp` — `wire_window_callbacks()`.
**Raised by:** Claude, GPT, Gemini (all three independently identified hardcoded shortcuts as a gap).

## Goal

Allow GUI keybindings to be configured in `config.toml` under a `[keybindings]` section.
Each keybinding maps a named action (`toggle_debug_panel`, `copy`, `paste`, `font_increase`,
`font_decrease`, `font_reset`) to a key combination string (`Ctrl+Shift+C`, `F12`, etc.).

## Implementation Plan

- [x] Define a `GuiKeybinding` struct with fields `action: std::string`, `key: SDL_Keycode`, `modifiers: SDL_Keymod`.
- [x] Add a `std::vector<GuiKeybinding> keybindings` field to `AppConfig` with sensible defaults.
- [x] Extend `AppConfig::load()` to parse a `[keybindings]` section. Each line is `action_name = "Mod+Key"`.
- [x] Write a key-string parser: `parse_key_combo("Ctrl+Shift+C") → {SDLK_c, KMOD_CTRL | KMOD_SHIFT}`.
- [x] In `wire_window_callbacks()`, replace the hardcoded `if (event.key == SDLK_F12)` checks with a loop over `config.keybindings`.
- [x] Provide a `dispatch_gui_action(action_name)` method on `App` that executes the named action.
- [x] Add round-trip tests for the key-string parser.
- [x] Update `CLAUDE.md` / user docs with the new `[keybindings]` section format.
- [x] Run `clang-format`.
- [x] Run `ctest --test-dir build`.

## Completion Notes

- Added configurable GUI-layer keybindings under `config.toml` `[keybindings]`.
- Preserved the historical `Ctrl+=` / `Ctrl+Plus` zoom-in behavior by treating `Ctrl+=` as the canonical config value and accepting the shifted alias at match time.
- Refactored `App` to dispatch named GUI actions (`toggle_debug_panel`, `copy`, `paste`, `font_increase`, `font_decrease`, `font_reset`) through config-driven bindings instead of a hardcoded callback ladder.
- Added parser, formatter, matcher, config round-trip, and table-override coverage in `tests/app_config_tests.cpp`.

## Notes

Do not implement key remapping for Neovim input (that would go through Neovim's own keymap system).
This feature covers only GUI-layer actions that Draxul handles before passing input to Neovim.

## Sub-Agent Split

- One agent on `AppConfig` parsing and key-string parser + tests.
- One agent on `wire_window_callbacks()` refactor to use the config-driven dispatch.

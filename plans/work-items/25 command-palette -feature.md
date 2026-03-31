# Command Palette

## Summary

Add a VS Code-style command palette (Ctrl+P) that lets the user fuzzy-search and execute GUI actions from a centered overlay.

## Tasks

- [x] Implement fzf-style fuzzy matcher (`app/fuzzy_match.h/.cpp`)
- [x] Add `action_names()` and `on_command_palette` to `GuiActionHandler`
- [x] Implement `CommandPalette` class (`app/command_palette.h/.cpp`)
- [x] Add Ctrl+P default keybinding in `app_config_io.cpp`
- [x] Route input through palette in `InputDispatcher`
- [x] Wire palette into `App` (member, rendering, action wiring)
- [x] Add new sources to `CMakeLists.txt`
- [x] Write unit tests (`tests/command_palette_tests.cpp`)
- [ ] Manual verification: open, filter, navigate, execute, escape

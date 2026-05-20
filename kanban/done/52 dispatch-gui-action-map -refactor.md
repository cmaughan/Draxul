# 52 Dispatch GUI Action Map

## Why This Exists

`App::dispatch_gui_action(std::string_view action)` is an O(n) if-else chain over string comparisons. As more GUI keybinding actions are added (font_increase, font_decrease, font_reset, toggle_debug_panel, copy, paste, etc.), this grows linearly. String comparison chains are fragile and hard to extend safely.

Identified by: **Claude** (smells #10).

## Goal

Replace the if-else chain with a `static const std::unordered_map<std::string_view, std::function<void()>>` (or a sorted compile-time array) built once per `App` instance, keyed on action name.

## Implementation Plan

- [x] Read `app/app.cpp` for `dispatch_gui_action` and list all current action names.
- [x] Define a private method `App::build_action_map()` that populates `action_map_` (a `std::unordered_map<std::string, std::function<void()>>` member) with all actions as lambdas capturing `this`.
- [x] Replace the if-else body with `auto it = action_map_.find(action); if (it != action_map_.end()) { it->second(); return true; } return false;`
- [x] Call `build_action_map()` in `App::initialize()` after `wire_window_callbacks()`.
- [x] Ensure lambdas capture `this` correctly.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Confined to `app.cpp`.

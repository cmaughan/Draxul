# 56 Live Config Reload

## Why This Exists

Currently, any change to `config.toml` — font family, font size, ligatures, keybindings — requires a full Draxul restart to take effect. This makes font and keybinding iteration slow and annoying. All three reviewers note this as a quality-of-life gap.

Identified by: **Claude** (QoL #2), implied by **GPT** (config GUI), **Gemini** (config GUI).

## Goal

Watch `config.toml` for filesystem changes and reload relevant config fields at runtime without requiring a restart. Font-affecting changes (family, size) trigger the font-size cascade (atlas reset, grid dirty, nvim resize). Keybinding changes reload the action map. Ligature changes reload the ligature enable flag.

## Implementation Plan

- [ ] Read `app/app_config.h` and `app/app_config.cpp` for the `AppConfig` structure and load path.
- [ ] Read `app/app.cpp` for `change_font_size`, `dispatch_gui_action`, and ligature enable paths.
- [ ] Choose a filesystem watcher strategy:
  - Option A: Poll `config.toml` mtime on each frame (simple, no dependency).
  - Option B: Use SDL's file watch events if available.
  - Option C: Add `efsw` or similar via FetchContent.
  - **Recommended**: Option A (poll on every 60th frame / ~1 second) to avoid a new dependency.
- [ ] Add a `ConfigWatcher` helper that checks mtime and calls a callback on change.
- [ ] In `App::run()`, after draining the nvim queue, call `config_watcher_.check()`.
- [ ] On change: reload `AppConfig`, diff against current config, apply changed fields:
  - Font family/size changed → `change_font_size()` equivalent + `reload_font()`.
  - Ligatures changed → update the ligature enable flag.
  - Keybindings changed → rebuild the action map.
- [ ] Log a message on successful reload.
- [ ] Add a test for `ConfigWatcher` using a temp file.
- [ ] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. `ConfigWatcher` is self-contained; the App wiring is a few lines.

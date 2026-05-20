# 62 Performance HUD

## Why This Exists

The debug panel shows some timing and atlas stats, but there is no lightweight persistent HUD for monitoring frame time, atlas usage, and RPC queue depth. Users who want to understand Draxul's performance must open the full debug panel (F12), which is distracting during actual editing.

Identified by: **GPT** (QoL #10), **Claude** (atlas reset noise is silent).

## Goal

Add a configurable lightweight HUD (a small semi-transparent overlay in a corner) showing: frame time (ms), atlas usage (%), RPC queue depth, and atlas reset count. Toggled by a keybinding (`perf_hud` action). Distinct from the full debug panel.

## Implementation Plan

- [ ] Read `app/app.cpp` for the ImGui render path and existing debug panel stats collection.
- [ ] Add `perf_hud` to the keybindings table and `dispatch_gui_action` map (item 52).
- [ ] Add a `PerfHud` class in `app/` with:
  - `render(ImGuiContext*, stats)` drawing a small semi-transparent `ImGui::Begin` window.
  - `toggle()` and `is_visible()`.
  - Displays: last frame time, rolling average, atlas fill %, atlas resets, RPC queue depth.
- [ ] Collect stats in `App::run()` (frame time already tracked; atlas fill % needs a `TextService` query; RPC queue depth needs an `NvimRpc` query).
- [ ] Add accessors to `TextService` and `NvimRpc` for the needed stats (non-blocking reads).
- [ ] Add `config.toml` option `show_perf_hud_on_startup = false`.
- [ ] Guard behind `DRAXUL_ENABLE_DEBUG_PANEL` (item 55) or its own flag.
- [ ] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. `PerfHud` is self-contained. Stats accessors are additive.

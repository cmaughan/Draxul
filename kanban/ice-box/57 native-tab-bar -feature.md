# 57 Native Tab Bar

## Why This Exists

Neovim's tabline is rendered as text inside the terminal grid, which is functional but looks primitive for a native GUI app. A native tab bar drawn by the GUI layer (using ImGui or platform APIs) would allow mouse tab switching without sending key sequences to nvim, and would look significantly more polished.

Identified by: **Claude** (QoL #6), **Gemini** (QoL #9).

## Goal

Render neovim tabpages as clickable tabs in the ImGui bottom/top panel. Clicking a tab sends `nvim_set_current_tabpage` via RPC. The neovim tabline highlight group provides tab labels.

## Implementation Plan

- [ ] Read `libs/draxul-nvim/src/ui_events.cpp` for how `tabline_update` or `set_title` events are handled.
- [ ] Read `app/app.cpp` for the `ui_panel_` (ImGui panel) integration.
- [ ] Read the neovim `ext_tabline` UI option documentation — Draxul may need to enable `ext_tabline` in the `nvim_ui_attach` options to receive tab events as structured data rather than rendered grid text.
- [ ] Add `ext_tabline: true` to the `nvim_ui_attach` call if not already present.
- [ ] Handle the `tabline_update` event: store tab labels and the current tab index.
- [ ] In the ImGui panel (or a new dedicated top strip), render one `ImGui::Tab` button per tabpage.
- [ ] On tab click: send `nvim_set_current_tabpage(tabpage_handle)` via `rpc_.notify()`.
- [ ] Suppress the grid-text tabline (the first row of the grid) when `ext_tabline` is active.
- [ ] Add a `config.toml` option `native_tab_bar = true` to allow opt-in.
- [ ] Run render snapshot tests and `clang-format`.

## Sub-Agent Split

Two agents: one handles `ext_tabline` RPC event handling and state storage, another handles the ImGui tab rendering and click dispatch. Coordinate on the shared tab state struct.

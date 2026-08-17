# Neovim tabpage strip

## Why This Exists

Draxul already has an application tab bar. This card is specifically for an optional,
nested strip representing Neovim tabpages inside an Nvim pane.

Identified by: **Claude** (QoL #6), **Gemini** (QoL #9).

## Goal

Render Neovim tabpages as clickable pane-local tabs without confusing them with Draxul
tabs or Spaces. Clicking a tab sends `nvim_set_current_tabpage` via RPC.

## Implementation Plan

- [ ] Read `libs/draxul-nvim/src/ui_events.cpp` for how `tabline_update` or `set_title` events are handled.
- [ ] Read `app/app.cpp` for the `ui_panel_` (ImGui panel) integration.
- [ ] Read the neovim `ext_tabline` UI option documentation — Draxul may need to enable `ext_tabline` in the `nvim_ui_attach` options to receive tab events as structured data rather than rendered grid text.
- [ ] Add `ext_tabline: true` to the `nvim_ui_attach` call if not already present.
- [ ] Handle the `tabline_update` event: store tab labels and the current tab index.
- [ ] Render a pane-local tabpage strip through the normal Chrome/overlay layout.
- [ ] On tab click: send `nvim_set_current_tabpage(tabpage_handle)` via `rpc_.notify()`.
- [ ] Suppress the grid-text tabline (the first row of the grid) when `ext_tabline` is active.
- [ ] Add a `config.toml` option `native_tab_bar = true` to allow opt-in.
- [ ] Run the focused Nvim/UI tests, relevant render snapshot, and same-cache smoke.

## Sub-Agent Split

Two agents: one handles `ext_tabline` RPC event handling and state storage, another handles the ImGui tab rendering and click dispatch. Coordinate on the shared tab state struct.

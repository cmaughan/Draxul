#pragma once

// Canonical registry of GUI action names recognized by Draxul.
//
// This header is the single source of truth for the names that can appear:
//   - in `[keybindings]` config keys (parsed by app_config_io.cpp)
//   - in keybinding combo parsing (keybinding_parser.cpp)
//   - in the runtime dispatch table (app/gui_action_handler.cpp)
//
// Adding a new action: add a row to `kGuiActions` and a dispatch entry in
// `gui_action_handler.cpp`. The `gui_action_parity_test` enforces that every
// canonical name has a corresponding dispatch entry.

#include <array>
#include <string>
#include <string_view>

namespace draxul
{

struct GuiActionInfo
{
    std::string_view name;
    // When true, this action is only valid in config files with a numeric
    // suffix `:1` .. `:9` (e.g. "activate_tab:3"). The bare name is rejected
    // by the config-key validator and is never written to disk.
    bool tab_indexed = false;
};

// Canonical list of GUI action base names. Order is fixed but not load-bearing.
inline constexpr std::array<GuiActionInfo, 58> kGuiActions = { {
    { "toggle_diagnostics" },
    { "copy" },
    { "paste" },
    { "confirm_paste" },
    { "cancel_paste" },
    { "toggle_copy_mode" },
    { "take_terminal_control" },
    { "font_increase" },
    { "font_decrease" },
    { "font_reset" },
    { "open_file_dialog" },
    { "split_vertical" },
    { "split_horizontal" },
    { "toggle_host_ui" },
    { "command_palette" },
    { "quit" },
    { "server_status" },
    { "open_server_log" },
    { "stop_server" },
    { "new_space" },
    { "switch_space" },
    { "rename_space" },
    { "close_space" },
    { "launch_agent" },
    { "attach_agent_identity" },
    { "focus_agent" },
    { "restart_agent" },
    { "clear_agent_identity" },
    { "explain_agent_state" },
    { "edit_config" },
    { "reload_config" },
    { "toggle_zoom" },
    { "close_pane" },
    { "restart_host" },
    { "swap_pane" },
    { "focus_left" },
    { "focus_right" },
    { "focus_up" },
    { "focus_down" },
    { "resize_pane_left" },
    { "resize_pane_right" },
    { "resize_pane_up" },
    { "resize_pane_down" },
    { "new_tab" },
    { "close_tab" },
    { "next_tab" },
    { "prev_tab" },
    { "activate_tab", true },
    { "rename_tab" },
    { "rename_pane" },
    { "move_tab_left" },
    { "move_tab_right" },
    { "duplicate_pane" },
    { "equalize_panes" },
    { "print_pane" },
    { "test_toast" },
} };

// Maximum tab index expanded for `tab_indexed` actions in config files.
inline constexpr int kGuiActionMaxTabIndex = 9;

// Look up the GuiActionInfo for a bare action name. Returns nullptr if unknown.
inline const GuiActionInfo* find_gui_action(std::string_view bare_name)
{
    for (const auto& info : kGuiActions)
    {
        if (info.name == bare_name)
            return &info;
    }
    return nullptr;
}

// Validate a config-file action key (the string before "=" in `[keybindings]`).
//   - bare names are valid only when `tab_indexed == false`
//   - tab-indexed actions require a `:N` suffix where N is 1..9
inline bool is_known_gui_action_config_key(std::string_view key)
{
    const auto colon = key.find(':');
    if (colon == std::string_view::npos)
    {
        const auto* info = find_gui_action(key);
        return info != nullptr && !info->tab_indexed;
    }
    const auto base = key.substr(0, colon);
    const auto* info = find_gui_action(base);
    if (info == nullptr || !info->tab_indexed)
        return false;
    const auto arg = key.substr(colon + 1);
    if (arg.size() != 1)
        return false;
    const char c = arg[0];
    return c >= '1' && c <= ('0' + kGuiActionMaxTabIndex);
}

// Invoke `f(std::string_view)` once for each config-key form in canonical order.
// Bare names are passed by reference into kGuiActions; expanded `name:N` keys are
// constructed in a temporary std::string and passed as a string_view valid for the
// duration of the call.
template <typename F>
void for_each_gui_action_config_key(F&& f)
{
    for (const auto& info : kGuiActions)
    {
        if (!info.tab_indexed)
        {
            f(info.name);
            continue;
        }
        std::string expanded;
        expanded.reserve(info.name.size() + 2);
        expanded.assign(info.name);
        expanded.push_back(':');
        expanded.push_back('0');
        for (int i = 1; i <= kGuiActionMaxTabIndex; ++i)
        {
            expanded.back() = static_cast<char>('0' + i);
            f(std::string_view{ expanded });
        }
    }
}

} // namespace draxul

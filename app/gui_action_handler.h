#pragma once

#include <draxul/host_kind.h>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

class TextService;
class UiPanel;
class IHost;
class IImGuiHost;
struct AppConfig;

// Handles named GUI actions: font_increase, font_decrease, font_reset, copy, paste,
// toggle_diagnostics. App owns an instance and calls execute() when a keybinding fires.
class GuiActionHandler
{
public:
    struct Deps
    {
        TextService* text_service = nullptr;
        UiPanel* ui_panel = nullptr;
        // Returns the currently focused host (null when no host is active).
        std::function<IHost*()> focused_host;
        IImGuiHost* imgui_host = nullptr;
        AppConfig* config = nullptr;

        // Callbacks into App for operations that need broader coordination
        std::function<void()> on_font_changed; // rebuild layout, set viewport, request_frame
        std::function<void()> on_panel_toggled; // refresh layout, set viewport, update panel, request_frame
        std::function<void()> on_config_changed; // persist config when save_user_config is enabled
        std::function<void()> on_open_file_dialog; // show the native file-open dialog
        std::function<void(std::optional<HostKind>)> on_split_vertical; // create a vertical split
        std::function<void(std::optional<HostKind>)> on_split_horizontal; // create a horizontal split
        std::function<void()> on_command_palette; // toggle command palette
        std::function<void()> on_edit_config; // open config in nvim side split
        std::function<void()> on_reload_config; // reload config.toml from disk
        std::function<void()> on_toggle_zoom; // zoom/unzoom the focused pane
        std::function<void()> on_close_pane; // close the focused pane
        std::function<void()> on_restart_host; // restart the host in the focused pane
        std::function<void()> on_swap_pane; // swap focused pane with the next one
        std::function<void()> on_focus_left; // move focus to left pane
        std::function<void()> on_focus_right; // move focus to right pane
        std::function<void()> on_focus_up; // move focus to upper pane
        std::function<void()> on_focus_down; // move focus to lower pane
        std::function<void(std::optional<HostKind>)> on_new_tab; // create a new workspace tab
        std::function<void()> on_close_tab; // close the active workspace tab
        std::function<void()> on_next_tab; // switch to the next tab
        std::function<void()> on_prev_tab; // switch to the previous tab
        std::function<void(int)> on_activate_tab; // switch to tab by 1-based index
    };

    explicit GuiActionHandler(Deps deps);

    // Returns true if the action was recognised and handled.
    // Optional args are forwarded to actions that accept parameters (e.g. split commands
    // accept a host kind like "zsh" or "megacity").
    bool execute(std::string_view action, std::string_view args = {});

    // Returns a sorted list of all registered action names.
    static std::vector<std::string_view> action_names();

private:
    using ActionFn = std::function<void(GuiActionHandler&, std::string_view args)>;
    static const std::unordered_map<std::string_view, ActionFn>& action_map();

    void font_increase();
    void font_decrease();
    void font_reset();
    void copy() const;
    void paste() const;
    void toggle_diagnostics() const;
    void open_file_dialog() const;
    void split_vertical(std::string_view args) const;
    void split_horizontal(std::string_view args) const;
    void toggle_megacity_ui() const;
    void toggle_zoom() const;
    void close_pane() const;
    void restart_host() const;
    void swap_pane() const;
    void focus_left() const;
    void focus_right() const;
    void focus_up() const;
    void focus_down() const;
    void new_tab(std::string_view args) const;
    void close_tab() const;
    void next_tab() const;
    void prev_tab() const;
    void activate_tab(std::string_view args) const;
    void change_font_size(float new_size);

    Deps deps_;
};

} // namespace draxul

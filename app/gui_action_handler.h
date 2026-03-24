#pragma once

#include <functional>
#include <string_view>
#include <unordered_map>

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
        std::function<void()> on_split_vertical; // create a vertical 2-pane split
        std::function<void()> on_split_horizontal; // create a horizontal 2-pane split
    };

    explicit GuiActionHandler(Deps deps);

    // Returns true if the action was recognised and handled.
    bool execute(std::string_view action);

private:
    using ActionFn = std::function<void(GuiActionHandler&)>;
    static const std::unordered_map<std::string_view, ActionFn>& action_map();

    void font_increase();
    void font_decrease();
    void font_reset();
    void copy() const;
    void paste() const;
    void toggle_diagnostics() const;
    void open_file_dialog() const;
    void split_vertical() const;
    void split_horizontal() const;
    void change_font_size(float new_size);

    Deps deps_;
};

} // namespace draxul

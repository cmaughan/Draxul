#pragma once

#include <cstddef>
#include <draxul/gui/palette_renderer.h>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul
{

struct GuiKeybinding;
struct KeyEvent;
struct TextInputEvent;
class GuiActionHandler;

class CommandPalette
{
public:
    struct Deps
    {
        GuiActionHandler* gui_action_handler = nullptr;
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        std::function<void()> request_frame;
    };

    CommandPalette();
    explicit CommandPalette(Deps deps);

    void open();
    void close();
    bool is_open() const;

    // Input handling — returns true when the palette consumed the event.
    bool on_key(const KeyEvent& event);
    bool on_text_input(const TextInputEvent& event);

    // Build the view state for rendering. Returns the data needed by render_palette().
    gui::PaletteViewState view_state(int grid_cols, int grid_rows, float panel_bg_alpha = 1.0f);

private:
    struct FilteredEntry
    {
        std::string_view action_name;
        std::string shortcut_hint;
        int score = 0;
        std::vector<size_t> match_positions;
    };

    std::pair<std::string_view, std::string_view> split_query() const;
    void refilter();
    void execute_selected();
    void move_selection(int delta);
    std::string shortcut_for_action(std::string_view action) const;

    Deps deps_;
    bool open_ = false;
    std::string query_;
    int selected_index_ = 0;
    std::vector<FilteredEntry> filtered_;
    std::vector<std::string_view> all_actions_;

    // Cached PaletteEntry views rebuilt by view_state().
    std::vector<gui::PaletteEntry> view_entries_;
};

} // namespace draxul

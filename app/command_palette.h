#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
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

    // True when the palette needs an ImGui frame (open, or just closed to clear focus).
    bool needs_render() const;

    // Input handling — returns true when the palette consumed the event.
    bool on_key(const KeyEvent& event);
    bool on_text_input(const TextInputEvent& event);

    // Render the palette overlay. Call within an active ImGui frame.
    void render(int window_width, int window_height);

private:
    struct FilteredEntry
    {
        std::string_view action_name;
        std::string shortcut_hint;
        int score = 0;
        std::vector<size_t> match_positions;
    };

    void refilter();
    void execute_selected();
    void move_selection(int delta);
    std::string shortcut_for_action(std::string_view action) const;

    Deps deps_;
    bool open_ = false;
    bool focus_input_ = false; // set on open, cleared after first ImGui frame
    bool needs_close_frame_ = false; // one more ImGui frame after close to clear focus
    std::string query_;
    int selected_index_ = 0;
    std::vector<FilteredEntry> filtered_;
    std::vector<std::string_view> all_actions_;
};

} // namespace draxul

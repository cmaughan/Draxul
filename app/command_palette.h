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
class HostProviderRegistry;

class CommandPalette
{
public:
    struct Deps
    {
        GuiActionHandler* gui_action_handler = nullptr;
        const HostProviderRegistry* host_registry = nullptr;
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        std::function<void()> request_frame;
        std::function<void()> on_closed; // called when the palette closes itself (Escape, execute)
    };

    struct PromptRequest
    {
        std::string title;
        std::string prompt;
        std::string initial_value;
        std::function<void(std::string)> on_submit;
        std::function<void()> on_cancel;
    };

    struct ChoiceEntry
    {
        std::string id;
        std::string name;
        std::string shortcut_hint;
        std::string search_text;
    };

    struct ChoiceRequest
    {
        std::string title;
        std::vector<ChoiceEntry> entries;
        std::function<void(std::string)> on_submit;
        std::function<void()> on_cancel;
    };

    CommandPalette();
    explicit CommandPalette(Deps deps);

    void open();
    void open_prompt(PromptRequest request);
    void open_choices(ChoiceRequest request);
    void close();
    bool is_open() const;

    // Input handling — returns true when the palette consumed the event.
    bool on_key(const KeyEvent& event);
    bool on_text_input(const TextInputEvent& event);

    // Build the view state for rendering. Returns the data needed by render_palette().
    gui::PaletteViewState view_state(int grid_cols, int grid_rows, float panel_bg_alpha = 1.0f);

private:
    enum class Mode
    {
        Actions,
        Prompt,
        Choices,
    };

    struct FilteredEntry
    {
        std::string_view name;
        std::string shortcut_hint;
        int score = 0;
        std::vector<size_t> match_positions;
        size_t choice_index = static_cast<size_t>(-1);
    };

    std::pair<std::string_view, std::string_view> split_query() const;
    void refilter();
    void execute_selected();
    void submit_choice();
    void submit_prompt();
    void cancel_prompt();
    void cancel_choice();
    void move_selection(int delta);
    std::string shortcut_for_action(std::string_view action) const;

    Deps deps_;
    bool open_ = false;
    Mode mode_ = Mode::Actions;
    PromptRequest prompt_;
    ChoiceRequest choices_;
    std::string prompt_message_;
    std::string query_;
    int selected_index_ = 0;
    std::vector<FilteredEntry> filtered_;
    std::vector<std::string> all_actions_;

    // Cached PaletteEntry views rebuilt by view_state().
    std::vector<gui::PaletteEntry> view_entries_;
};

} // namespace draxul

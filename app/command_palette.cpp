#include "command_palette.h"

#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/keybinding_parser.h>

namespace draxul
{

CommandPalette::CommandPalette() = default;

CommandPalette::CommandPalette(Deps deps)
    : deps_(std::move(deps))
{
}

void CommandPalette::open()
{
    if (open_)
        return;
    open_ = true;
    query_.clear();
    selected_index_ = 0;

    // Cache action names, excluding the palette's own action
    all_actions_.clear();
    for (auto name : GuiActionHandler::action_names())
    {
        if (name != "command_palette")
            all_actions_.push_back(name);
    }
    refilter();

    if (deps_.request_frame)
        deps_.request_frame();
}

void CommandPalette::close()
{
    open_ = false;
    if (deps_.request_frame)
        deps_.request_frame();
}

bool CommandPalette::is_open() const
{
    return open_;
}

bool CommandPalette::on_key(const KeyEvent& event)
{
    if (!open_)
        return false;

    if (!event.pressed)
        return true; // consume key-up too

    const bool ctrl = (event.mod & kModCtrl) != 0;
    const bool shift = (event.mod & kModShift) != 0;

    if (event.keycode == SDLK_ESCAPE)
    {
        close();
        return true;
    }
    if (event.keycode == SDLK_RETURN || event.keycode == SDLK_KP_ENTER)
    {
        execute_selected();
        return true;
    }
    if ((ctrl && event.keycode == SDLK_J) || event.keycode == SDLK_DOWN)
    {
        move_selection(1);
        return true;
    }
    if ((ctrl && event.keycode == SDLK_K) || event.keycode == SDLK_UP)
    {
        move_selection(-1);
        return true;
    }
    if (event.keycode == SDLK_BACKSPACE)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            refilter();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    // Tab: autocomplete query to selected entry name.
    if (event.keycode == SDLK_TAB)
    {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(filtered_.size()))
        {
            query_ = std::string(filtered_[static_cast<size_t>(selected_index_)].action_name);
            refilter();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    // Ctrl+Shift+P while open = close (toggle)
    if (ctrl && shift && event.keycode == SDLK_P)
    {
        close();
        return true;
    }

    // Consume all other keys to block host
    return true;
}

bool CommandPalette::on_text_input(const TextInputEvent& event)
{
    if (!open_)
        return false;

    query_ += event.text;
    refilter();
    if (deps_.request_frame)
        deps_.request_frame();
    return true;
}

std::pair<std::string_view, std::string_view> CommandPalette::split_query() const
{
    auto pos = query_.find(' ');
    if (pos == std::string::npos)
        return { query_, {} };
    return { std::string_view(query_).substr(0, pos),
        std::string_view(query_).substr(pos + 1) };
}

void CommandPalette::refilter()
{
    filtered_.clear();
    const auto [command, args] = split_query();

    for (auto name : all_actions_)
    {
        if (command.empty())
        {
            filtered_.push_back({ name, shortcut_for_action(name), 0, {} });
        }
        else
        {
            auto result = fuzzy_match(command, name);
            if (result.matched)
                filtered_.push_back({ name, shortcut_for_action(name), result.score, std::move(result.positions) });
        }
    }

    if (!command.empty())
    {
        std::sort(filtered_.begin(), filtered_.end(), [](const FilteredEntry& a, const FilteredEntry& b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.action_name.size() != b.action_name.size())
                return a.action_name.size() < b.action_name.size();
            return a.action_name < b.action_name;
        });
    }

    selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(filtered_.size()) - 1));
}

void CommandPalette::execute_selected()
{
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(filtered_.size()))
    {
        const auto action = filtered_[static_cast<size_t>(selected_index_)].action_name;
        const auto [command, args] = split_query();
        close();
        if (deps_.gui_action_handler)
            deps_.gui_action_handler->execute(action, args);
    }
    else
    {
        close();
    }
}

void CommandPalette::move_selection(int delta)
{
    if (filtered_.empty())
        return;
    selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(filtered_.size()) - 1);
    if (deps_.request_frame)
        deps_.request_frame();
}

std::string CommandPalette::shortcut_for_action(std::string_view action) const
{
    if (!deps_.keybindings)
        return {};
    for (const auto& binding : *deps_.keybindings)
    {
        if (binding.action == action)
        {
            if (binding.prefix_key != 0)
            {
                return format_gui_keybinding_combo(binding.prefix_key, binding.prefix_modifiers) + ", "
                    + format_gui_keybinding_combo(binding.key, binding.modifiers);
            }
            return format_gui_keybinding_combo(binding.key, binding.modifiers);
        }
    }
    return {};
}

gui::PaletteViewState CommandPalette::view_state(int grid_cols, int grid_rows, float panel_bg_alpha)
{
    // Build PaletteEntry views from filtered entries.
    view_entries_.clear();
    view_entries_.reserve(filtered_.size());
    for (const auto& f : filtered_)
    {
        view_entries_.push_back({
            f.action_name,
            f.shortcut_hint,
            f.match_positions,
        });
    }

    gui::PaletteViewState vs;
    vs.grid_cols = grid_cols;
    vs.grid_rows = grid_rows;
    vs.query = query_;
    vs.selected_index = selected_index_;
    vs.entries = view_entries_;
    vs.panel_bg_alpha = panel_bg_alpha;
    return vs;
}

} // namespace draxul

#include "command_palette.h"

#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host_kind.h>
#include <draxul/host_registry.h>
#include <draxul/keybinding_parser.h>
#include <unordered_set>

namespace draxul
{

namespace
{

std::string trim_copy(std::string_view value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return std::string(value.substr(begin, end - begin));
}

} // namespace

CommandPalette::CommandPalette() = default;

CommandPalette::CommandPalette(Deps deps)
    : deps_(std::move(deps))
{
}

void CommandPalette::open()
{
    open_ = true;
    mode_ = Mode::Actions;
    prompt_ = PromptRequest{};
    choices_ = ChoiceRequest{};
    prompt_message_.clear();
    query_.clear();
    selected_index_ = 0;

    // Cache action names, excluding the palette's own action.
    // Actions that accept a host-kind argument are expanded into compound
    // entries (e.g. "split_vertical zsh") so the user can fuzzy-match both
    // the command and the host type in a single query.
    all_actions_.clear();

    const HostProviderRegistry& registry = deps_.host_registry != nullptr
        ? *deps_.host_registry
        : HostProviderRegistry::global();

    static const std::unordered_set<std::string_view> kHostArgActions = {
        "split_vertical",
        "split_horizontal",
        "new_tab",
    };

    // Actions that require a numeric tab index — expand into concrete entries
    // (e.g. "Activate Tab 1" … "Activate Tab 9") and suppress the bare entry.
    static const std::unordered_set<std::string_view> kTabIndexActions = {
        "activate_tab",
    };

    for (auto name : GuiActionHandler::action_names())
    {
        if (name == "command_palette")
            continue;
        if (deps_.action_visible
            && !deps_.action_visible(name))
        {
            continue;
        }
        if (kTabIndexActions.count(name))
        {
            for (int i = 1; i <= 9; ++i)
                all_actions_.push_back(std::string(name) + " " + std::to_string(i));
            continue;
        }
        all_actions_.emplace_back(std::string(name));
        if (kHostArgActions.count(name))
        {
            const HostLaunchContext context = name == "new_tab"
                ? HostLaunchContext::NewTab
                : HostLaunchContext::Split;
            for (const auto& provider : registry.available_providers())
            {
                if (provider.palette_visible && !provider.test_only
                    && supports_launch_context(provider.launch_contexts, context))
                {
                    all_actions_.push_back(std::string(name) + " " + provider.canonical_cli_name);
                }
            }
        }
    }
    refilter();

    if (deps_.request_frame)
        deps_.request_frame();
}

void CommandPalette::open_prompt(PromptRequest request)
{
    open_ = true;
    mode_ = Mode::Prompt;
    prompt_ = std::move(request);
    choices_ = ChoiceRequest{};
    query_ = prompt_.initial_value;
    prompt_message_.clear();
    selected_index_ = -1;
    filtered_.clear();
    all_actions_.clear();

    if (deps_.request_frame)
        deps_.request_frame();
}

void CommandPalette::open_choices(ChoiceRequest request)
{
    open_ = true;
    mode_ = Mode::Choices;
    prompt_ = PromptRequest{};
    choices_ = std::move(request);
    prompt_message_.clear();
    query_.clear();
    selected_index_ = 0;
    all_actions_.clear();
    refilter();

    if (deps_.request_frame)
        deps_.request_frame();
}

void CommandPalette::close()
{
    if (!open_)
        return;
    open_ = false;
    if (deps_.on_closed)
        deps_.on_closed();
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
        if (mode_ == Mode::Prompt)
            cancel_prompt();
        else if (mode_ == Mode::Choices)
            cancel_choice();
        else
            close();
        return true;
    }
    if (event.keycode == SDLK_RETURN || event.keycode == SDLK_KP_ENTER)
    {
        if (mode_ == Mode::Prompt)
            submit_prompt();
        else if (mode_ == Mode::Choices)
            submit_choice();
        else
            execute_selected();
        return true;
    }
    if (event.keycode == SDLK_BACKSPACE)
    {
        const bool had_message = !prompt_message_.empty();
        if (!query_.empty())
        {
            query_.pop_back();
            prompt_message_.clear();
            if (mode_ == Mode::Actions || mode_ == Mode::Choices)
                refilter();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        else if (had_message)
        {
            prompt_message_.clear();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }

    if (mode_ == Mode::Prompt)
        return true;

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
    // Tab: autocomplete query to selected entry name.
    if (event.keycode == SDLK_TAB)
    {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(filtered_.size()))
        {
            query_ = std::string(filtered_[static_cast<size_t>(selected_index_)].name);
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
    prompt_message_.clear();
    if (mode_ == Mode::Actions || mode_ == Mode::Choices)
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
    const auto split = split_query();
    const std::string_view command = split.first;

    if (mode_ == Mode::Choices)
    {
        for (size_t i = 0; i < choices_.entries.size(); ++i)
        {
            const auto& choice = choices_.entries[i];
            if (query_.empty())
            {
                filtered_.push_back({ choice.name, choice.shortcut_hint, 0, {}, i });
                continue;
            }

            auto name_match = fuzzy_match(query_, choice.name);
            const std::string_view search_text = choice.search_text.empty()
                ? std::string_view(choice.name)
                : std::string_view(choice.search_text);
            auto search_match = search_text == choice.name
                ? name_match
                : fuzzy_match(query_, search_text);
            if (!name_match.matched && !search_match.matched)
                continue;

            int score = 0;
            std::vector<size_t> positions;
            if (name_match.matched)
            {
                score = name_match.score;
                positions = std::move(name_match.positions);
            }
            if (search_match.matched && search_match.score > score)
                score = search_match.score;

            filtered_.push_back({ choice.name, choice.shortcut_hint, score, std::move(positions), i });
        }

        if (!query_.empty())
        {
            std::sort(filtered_.begin(), filtered_.end(), [](const FilteredEntry& a, const FilteredEntry& b) {
                if (a.score != b.score)
                    return a.score > b.score;
                if (a.name.size() != b.name.size())
                    return a.name.size() < b.name.size();
                return a.name < b.name;
            });
        }

        selected_index_ = filtered_.empty()
            ? -1
            : std::clamp(selected_index_, 0, static_cast<int>(filtered_.size()) - 1);
        return;
    }

    for (const auto& name : all_actions_)
    {
        if (command.empty())
        {
            filtered_.push_back({ name, shortcut_for_action(name), 0, {} });
        }
        else
        {
            // Fuzzy match against the full compound name (e.g. "split_vertical zsh").
            auto result = fuzzy_match(query_, name);
            if (result.matched)
                filtered_.push_back({ name, shortcut_for_action(name), result.score, std::move(result.positions) });
        }
    }

    if (!command.empty())
    {
        std::sort(filtered_.begin(), filtered_.end(), [](const FilteredEntry& a, const FilteredEntry& b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.name.size() != b.name.size())
                return a.name.size() < b.name.size();
            return a.name < b.name;
        });
    }

    selected_index_ = filtered_.empty()
        ? -1
        : std::clamp(selected_index_, 0, static_cast<int>(filtered_.size()) - 1);
}

void CommandPalette::execute_selected()
{
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(filtered_.size()))
    {
        const std::string entry(filtered_[static_cast<size_t>(selected_index_)].name);
        // Split compound entry (e.g. "split_vertical zsh") into action + args.
        const auto space = entry.find(' ');
        const std::string_view action = std::string_view(entry).substr(0, space);
        const std::string_view args = space != std::string::npos
            ? std::string_view(entry).substr(space + 1)
            : std::string_view{};
        close();
        if (deps_.gui_action_handler)
            deps_.gui_action_handler->execute(action, args);
    }
    else
    {
        close();
    }
}

void CommandPalette::submit_choice()
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_.size()))
        return;

    const size_t choice_index = filtered_[static_cast<size_t>(selected_index_)].choice_index;
    if (choice_index >= choices_.entries.size())
        return;

    const std::string id = choices_.entries[choice_index].id;
    auto callback = std::move(choices_.on_submit);
    close();
    if (callback)
        callback(id);
}

void CommandPalette::submit_prompt()
{
    std::string value = trim_copy(query_);
    if (value.empty())
    {
        prompt_message_ = prompt_.empty_message;
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }

    auto callback = std::move(prompt_.on_submit);
    close();
    if (callback)
        callback(std::move(value));
}

void CommandPalette::cancel_prompt()
{
    auto callback = std::move(prompt_.on_cancel);
    close();
    if (callback)
        callback();
}

void CommandPalette::cancel_choice()
{
    auto callback = std::move(choices_.on_cancel);
    close();
    if (callback)
        callback();
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
    if (mode_ == Mode::Actions)
    {
        view_entries_.reserve(filtered_.size());
        for (const auto& f : filtered_)
        {
            view_entries_.push_back({
                f.name,
                f.shortcut_hint,
                f.match_positions,
            });
        }
    }
    else if (mode_ == Mode::Choices)
    {
        view_entries_.reserve(filtered_.size());
        for (const auto& f : filtered_)
        {
            view_entries_.push_back({
                f.name,
                f.shortcut_hint,
                f.match_positions,
            });
        }
    }

    gui::PaletteViewState vs;
    vs.mode = mode_ == Mode::Prompt ? gui::PaletteMode::Prompt : gui::PaletteMode::Actions;
    vs.grid_cols = grid_cols;
    vs.grid_rows = grid_rows;
    vs.title = prompt_.title;
    vs.prompt = prompt_.prompt;
    vs.query = query_;
    vs.message = prompt_message_;
    vs.selected_index = mode_ == Mode::Prompt ? -1 : selected_index_;
    vs.entries = view_entries_;
    vs.panel_bg_alpha = panel_bg_alpha;
    return vs;
}

} // namespace draxul

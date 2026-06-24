#include "session_picker.h"

#include "fuzzy_match.h"
#include "session_listing.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace draxul
{

SessionPicker::SessionPicker() = default;

SessionPicker::SessionPicker(Deps deps)
    : deps_(std::move(deps))
{
}

void SessionPicker::refresh_sessions()
{
    std::string error;
    sessions_.clear();
    if (deps_.list_sessions)
        sessions_ = deps_.list_sessions(&error);
    if (!error.empty() && deps_.report_error)
        deps_.report_error(error);
    refilter(true);
    if (deps_.request_frame)
        deps_.request_frame();
}

bool SessionPicker::on_key(const KeyEvent& event)
{
    if (!event.pressed)
        return true;

    const bool ctrl = (event.mod & kModCtrl) != 0;
    if (event.keycode == SDLK_ESCAPE)
    {
        if (deps_.request_quit)
            deps_.request_quit();
        return true;
    }
    if (event.keycode == SDLK_RETURN || event.keycode == SDLK_KP_ENTER)
    {
        activate_selected();
        return true;
    }
    if ((ctrl && event.keycode == SDLK_J) || event.keycode == SDLK_DOWN)
    {
        if (!filtered_.empty())
        {
            selected_index_ = std::clamp(selected_index_ + 1, 0, static_cast<int>(filtered_.size()) - 1);
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    if ((ctrl && event.keycode == SDLK_K) || event.keycode == SDLK_UP)
    {
        if (!filtered_.empty())
        {
            selected_index_ = std::clamp(selected_index_ - 1, 0, static_cast<int>(filtered_.size()) - 1);
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    if (event.keycode == SDLK_BACKSPACE)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            refilter(true);
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    if (event.keycode == SDLK_DELETE)
    {
        kill_selected();
        return true;
    }
    if (event.keycode == SDLK_F5 || (ctrl && event.keycode == SDLK_R))
    {
        refresh_sessions();
        return true;
    }

    return true;
}

bool SessionPicker::on_text_input(const TextInputEvent& event)
{
    query_ += event.text;
    refilter(true);
    if (deps_.request_frame)
        deps_.request_frame();
    return true;
}

gui::PaletteViewState SessionPicker::view_state(int grid_cols, int grid_rows, float panel_bg_alpha)
{
    view_entries_.clear();
    view_entries_.reserve(filtered_.size());
    for (const auto& entry : filtered_)
    {
        view_entries_.push_back({
            entry.name,
            entry.shortcut_hint,
            entry.match_positions,
        });
    }

    gui::PaletteViewState state;
    state.grid_cols = grid_cols;
    state.grid_rows = grid_rows;
    state.query = query_;
    state.selected_index = selected_index_;
    state.entries = view_entries_;
    state.panel_bg_alpha = panel_bg_alpha;
    return state;
}

void SessionPicker::refilter(bool reset_selection)
{
    filtered_.clear();

    FilteredEntry create_entry;
    create_entry.kind = EntryKind::NewSession;
    create_entry.name = trimmed_query().empty()
        ? "new-session"
        : "new-session " + trimmed_query();
    create_entry.shortcut_hint = "Enter";
    filtered_.push_back(std::move(create_entry));

    struct RankedEntry
    {
        FilteredEntry entry;
        int score = 0;
    };
    std::vector<RankedEntry> matches;
    matches.reserve(sessions_.size());

    for (const auto& session : sessions_)
    {
        FilteredEntry entry;
        entry.kind = EntryKind::Session;
        entry.session = session;
        entry.name = session_entry_name(session);
        entry.shortcut_hint = session_entry_hint(session);

        int score = 0;
        if (!query_.empty())
        {
            auto name_match = fuzzy_match(query_, entry.name);
            auto id_match = fuzzy_match(query_, session.session_id);
            if (!name_match.matched && !id_match.matched)
                continue;
            if (name_match.matched)
            {
                entry.match_positions = std::move(name_match.positions);
                score = name_match.score;
            }
            else
            {
                score = id_match.score;
            }
        }

        matches.push_back({ std::move(entry), score });
    }

    if (!query_.empty())
    {
        std::sort(matches.begin(), matches.end(), [](const RankedEntry& lhs, const RankedEntry& rhs) {
            if (lhs.score != rhs.score)
                return lhs.score > rhs.score;
            return lhs.entry.name < rhs.entry.name;
        });
    }

    for (auto& match : matches)
    {
        match.entry.score = match.score;
        filtered_.push_back(std::move(match.entry));
    }

    if (reset_selection)
        selected_index_ = default_selection_index();
    else
        selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(filtered_.size()) - 1));
}

bool SessionPicker::activate_selected()
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_.size()))
        return false;

    const auto& entry = filtered_[static_cast<size_t>(selected_index_)];
    std::string error;
    bool ok = false;
    if (entry.kind == EntryKind::NewSession)
    {
        ok = deps_.create_session && deps_.create_session(trimmed_query(), &error);
    }
    else
    {
        ok = deps_.activate_session && deps_.activate_session(entry.session.session_id, &error);
    }

    if (!ok)
    {
        if (!error.empty() && deps_.report_error)
            deps_.report_error(error);
        return false;
    }

    if (deps_.request_quit)
        deps_.request_quit();
    return true;
}

bool SessionPicker::kill_selected()
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_.size()))
        return false;

    const auto& entry = filtered_[static_cast<size_t>(selected_index_)];
    if (entry.kind != EntryKind::Session || !deps_.kill_session)
        return false;

    std::string error;
    if (!deps_.kill_session(entry.session.session_id, &error))
    {
        if (!error.empty() && deps_.report_error)
            deps_.report_error(error);
        return false;
    }

    refresh_sessions();
    return true;
}

std::string SessionPicker::trimmed_query() const
{
    std::string_view text = query_;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

int SessionPicker::default_selection_index() const
{
    if (filtered_.size() > 1 && trimmed_query().empty())
        return 1;
    return 0;
}

} // namespace draxul

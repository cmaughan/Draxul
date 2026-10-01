#include <draxul/personal_assistant_host.h>
#include <draxul/unicode.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <sstream>

namespace draxul
{
void PersonalAssistantHost::on_viewport_changed()
{
    apply_grid_size(std::max(1, viewport().grid_size.x), std::max(1, viewport().grid_size.y));
    dirty_ = true;
}

bool PersonalAssistantHost::initialize_host()
{
    highlights().set_default_bg(color_from_rgb(0x101318));
    highlights().set_default_fg(color_from_rgb(0xD8DEE9));
    grid_pipeline().set_enable_ligatures(false);
    set_cursor_busy(true);
    selected_id_ = launch_options().source_path;
    running_ = true;
    callbacks().set_window_title("Personal Assistant");
    pump();
    return true;
}

void PersonalAssistantHost::pump()
{
    if (!running_)
        return;
    const auto current = callbacks().personal_agents();
    if (current != snapshot_)
    {
        snapshot_ = current;
        if (snapshot_ && !snapshot_->agents.empty())
        {
            const auto found = std::ranges::find(snapshot_->agents, selected_id_, &PersonalAgentDefinition::id);
            selected_ = found == snapshot_->agents.end() ? 0 : static_cast<size_t>(found - snapshot_->agents.begin());
            selected_id_ = snapshot_->agents[selected_].id;
        }
        dirty_ = true;
    }
    if (dirty_)
        redraw();
}

void PersonalAssistantHost::on_key(const KeyEvent& event)
{
    if (!event.pressed || !snapshot_ || snapshot_->agents.empty())
        return;
    if (event.keycode == SDLK_DOWN || event.keycode == SDLK_J)
        selected_ = std::min(selected_ + 1, snapshot_->agents.size() - 1);
    else if (event.keycode == SDLK_UP || event.keycode == SDLK_K)
        selected_ = selected_ == 0 ? 0 : selected_ - 1;
    else if (event.keycode == SDLK_PAGEDOWN)
        ++instruction_offset_;
    else if (event.keycode == SDLK_PAGEUP)
        instruction_offset_ = instruction_offset_ == 0 ? 0 : instruction_offset_ - 1;
    else
        return;
    if (selected_id_ != snapshot_->agents[selected_].id)
        instruction_offset_ = 0;
    selected_id_ = snapshot_->agents[selected_].id;
    dirty_ = true;
    callbacks().request_frame();
}

bool PersonalAssistantHost::dispatch_action(std::string_view action)
{
    constexpr std::string_view prefix = "personal.select/";
    if (!action.starts_with(prefix))
        return false;
    selected_id_ = action.substr(prefix.size());
    snapshot_.reset();
    instruction_offset_ = 0;
    pump();
    return true;
}

void PersonalAssistantHost::write_line(int row, std::string_view text)
{
    if (row < 0 || row >= grid_rows())
        return;
    int column = 1;
    for (const auto& cluster : display_clusters(text))
    {
        if (column + cluster.cell_width > grid_cols() - 1)
            break;
        grid().set_cell(column, row, cluster.text, 0, cluster.cell_width == 2);
        column += cluster.cell_width;
    }
}

void PersonalAssistantHost::redraw()
{
    grid().clear();
    write_line(0, "PERSONAL ASSISTANT | Read only - execution disabled");
    write_line(1, "Up/Down: select | PgUp/PgDn: instructions");
    if (!snapshot_)
        write_line(3, "Waiting for a shared-server collection connection.");
    else if (snapshot_->root.empty())
        write_line(3, snapshot_->error.empty()
                ? "Configure [agents] personal_root in config.toml, then restart the server."
                : snapshot_->error);
    else
    {
        write_line(3, snapshot_->name + " | " + snapshot_->root);
        write_line(4, snapshot_->error.empty() ? "Ownership not configured. Nothing will run." : snapshot_->error);
        if (snapshot_->agents.empty())
            write_line(6, "No definitions found in agents/.");
        else
        {
            selected_ = std::min(selected_, snapshot_->agents.size() - 1);
            const int visible = std::max(1, std::min(6, grid_rows() / 4));
            const size_t first = selected_ >= static_cast<size_t>(visible) ? selected_ - visible + 1 : 0;
            int row = 6;
            for (size_t index = first; index < snapshot_->agents.size() && index < first + visible; ++index)
            {
                const auto& definition = snapshot_->agents[index];
                write_line(row++, (index == selected_ ? "> " : "  ") + definition.name
                    + (definition.error.empty() ? " [read only]" : " [invalid / last known]"));
            }
            const auto& definition = snapshot_->agents[selected_];
            ++row;
            write_line(row++, definition.name + " | " + definition.profile + " / " + definition.model);
            write_line(row++, "Revision " + std::to_string(definition.revision) + " | every "
                + std::to_string(definition.interval_seconds) + "s | configured " + (definition.enabled ? "enabled" : "disabled"));
            if (!definition.error.empty())
                write_line(row++, definition.error);
            write_line(row++, "INSTRUCTIONS");
            std::vector<std::string> lines;
            std::istringstream input(definition.instructions);
            std::string line;
            while (std::getline(input, line))
            {
                std::string wrapped;
                int columns = 0;
                for (const auto& cluster : display_clusters(line))
                {
                    if (columns + cluster.cell_width > std::max(1, grid_cols() - 2))
                    {
                        lines.push_back(std::move(wrapped));
                        wrapped.clear();
                        columns = 0;
                    }
                    wrapped += cluster.text;
                    columns += cluster.cell_width;
                }
                lines.push_back(std::move(wrapped));
            }
            instruction_offset_ = std::min(instruction_offset_, lines.empty() ? 0 : lines.size() - 1);
            for (size_t index = instruction_offset_; index < lines.size() && row < grid_rows(); ++index)
                write_line(row++, lines[index]);
        }
    }
    set_content_ready(true);
    flush_grid();
    dirty_ = false;
    callbacks().request_frame();
}

void register_personal_assistant_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::PersonalAssistant, [] { return std::make_unique<PersonalAssistantHost>(); });
}
}

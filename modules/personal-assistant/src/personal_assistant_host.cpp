#include <draxul/personal_assistant_host.h>
#include <draxul/unicode.h>
#include <SDL3/SDL_keycode.h>
#include <algorithm>

namespace draxul
{
void PersonalAssistantHost::on_viewport_changed()
{
    apply_grid_size(std::max(1,viewport().grid_size.x),std::max(1,viewport().grid_size.y));
    dirty_=true;
}
bool PersonalAssistantHost::initialize_host()
{
    highlights().set_default_bg(color_from_rgb(0x101318));
    highlights().set_default_fg(color_from_rgb(0xD8DEE9));
    grid_pipeline().set_enable_ligatures(false);
    set_cursor_busy(true);
    selected_id_=launch_options().source_path;
    running_=true;
    callbacks().set_window_title("Personal Assistant");
    pump();
    return true;
}
void PersonalAssistantHost::pump()
{
    if (!running_) return;
    const auto current=callbacks().personal_agents();
    if (current!=snapshot_) { snapshot_=current; dirty_=true; }
    if (dirty_) redraw();
}
void PersonalAssistantHost::on_key(const KeyEvent& event)
{
    if (!event.pressed) return;
    if (event.keycode==SDLK_N) callbacks().open_personal_agent({});
    else if (event.keycode==SDLK_RETURN) callbacks().open_personal_agent(selected_id_);
}
bool PersonalAssistantHost::dispatch_action(std::string_view action)
{
    if (action=="personal.new") { callbacks().open_personal_agent({}); return true; }
    if (!action.starts_with("personal.select/")) return false;
    selected_id_=action.substr(16);
    dirty_=true;
    pump();
    return true;
}
void PersonalAssistantHost::write_line(int row,std::string_view text)
{
    if (row<0 || row>=grid_rows()) return;
    int column=1;
    for (const auto& cluster:display_clusters(text))
    {
        if (column+cluster.cell_width>grid_cols()-1) break;
        grid().set_cell(column,row,cluster.text,0,cluster.cell_width==2);
        column+=cluster.cell_width;
    }
}
void PersonalAssistantHost::redraw()
{
    grid().clear();
    write_line(0,"PERSONAL AGENTS");
    write_line(2,"Start an agent, then chat in its terminal.");
    write_line(4,"N: new agent   Enter: open conversation");
    write_line(6,"Click its sidebar pill to return to the chat.");
    write_line(7,"Double-click to rename; right-click to delete.");
    write_line(9,"Instructions and working data live in Dropbox.");
    write_line(10,"The agent loads them when its conversation starts.");
    if (!snapshot_) write_line(12,"Waiting for the shared server...");
    else if (!snapshot_->error.empty()) write_line(12,snapshot_->error);
    else if (snapshot_->root.empty()) write_line(12,"Configure [agents] personal_root, then restart the server.");
    else write_line(12,snapshot_->root);
    set_content_ready(true);
    flush_grid();
    dirty_=false;
    callbacks().request_frame();
}
void register_personal_assistant_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::PersonalAssistant,[]{return std::make_unique<PersonalAssistantHost>();});
}
}

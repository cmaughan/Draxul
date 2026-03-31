#include "gui_action_handler.h"

#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <draxul/ui_panel.h>

namespace draxul
{

GuiActionHandler::GuiActionHandler(Deps deps)
    : deps_(std::move(deps))
{
}

const std::unordered_map<std::string_view, GuiActionHandler::ActionFn>& GuiActionHandler::action_map()
{
    // clang-format off
    static const std::unordered_map<std::string_view, ActionFn> map = {
        {"toggle_diagnostics", [](auto& h, auto) { h.toggle_diagnostics(); }},
        {"copy",               [](auto& h, auto) { h.copy(); }},
        {"paste",              [](auto& h, auto) { h.paste(); }},
        {"font_increase",      [](auto& h, auto) { h.font_increase(); }},
        {"font_decrease",      [](auto& h, auto) { h.font_decrease(); }},
        {"font_reset",         [](auto& h, auto) { h.font_reset(); }},
        {"open_file_dialog",   [](auto& h, auto) { h.open_file_dialog(); }},
        {"split_vertical",     [](auto& h, auto args) { h.split_vertical(args); }},
        {"split_horizontal",   [](auto& h, auto args) { h.split_horizontal(args); }},
        {"toggle_host_ui",     [](auto& h, auto) { h.toggle_host_ui(); }},
        {"command_palette",    [](auto& h, auto) { if (h.deps_.on_command_palette) h.deps_.on_command_palette(); }},
        {"edit_config",        [](auto& h, auto) { if (h.deps_.on_edit_config) h.deps_.on_edit_config(); }},
    };
    // clang-format on
    return map;
}

bool GuiActionHandler::execute(std::string_view action, std::string_view args)
{
    PERF_MEASURE();
    const auto& map = action_map();
    if (const auto it = map.find(action); it != map.end())
    {
        it->second(*this, args);
        return true;
    }
    DRAXUL_LOG_WARN(LogCategory::App, "Unknown GUI action: '%.*s'",
        static_cast<int>(action.size()), action.data());
    return false;
}

std::vector<std::string_view> GuiActionHandler::action_names()
{
    const auto& map = action_map();
    std::vector<std::string_view> names;
    names.reserve(map.size());
    for (const auto& [name, _] : map)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

void GuiActionHandler::font_increase()
{
    PERF_MEASURE();
    change_font_size(deps_.text_service->point_size() + 0.5f);
}

void GuiActionHandler::font_decrease()
{
    PERF_MEASURE();
    change_font_size(deps_.text_service->point_size() - 0.5f);
}

void GuiActionHandler::font_reset()
{
    PERF_MEASURE();
    change_font_size(TextService::DEFAULT_POINT_SIZE);
}

void GuiActionHandler::copy() const
{
    PERF_MEASURE();
    IHost* host = deps_.focused_host ? deps_.focused_host() : nullptr;
    if (host)
        host->dispatch_action("copy");
}

void GuiActionHandler::paste() const
{
    PERF_MEASURE();
    IHost* host = deps_.focused_host ? deps_.focused_host() : nullptr;
    if (host)
        host->dispatch_action("paste");
}

void GuiActionHandler::toggle_diagnostics() const
{
    PERF_MEASURE();
    deps_.ui_panel->toggle_visible();
    if (deps_.on_panel_toggled)
        deps_.on_panel_toggled();
}

void GuiActionHandler::open_file_dialog() const
{
    PERF_MEASURE();
    if (deps_.on_open_file_dialog)
        deps_.on_open_file_dialog();
}

void GuiActionHandler::split_vertical(std::string_view args) const
{
    PERF_MEASURE();
    if (deps_.on_split_vertical)
        deps_.on_split_vertical(parse_host_kind(args));
}

void GuiActionHandler::split_horizontal(std::string_view args) const
{
    PERF_MEASURE();
    if (deps_.on_split_horizontal)
        deps_.on_split_horizontal(parse_host_kind(args));
}

void GuiActionHandler::toggle_host_ui() const
{
    PERF_MEASURE();
    IHost* host = deps_.focused_host ? deps_.focused_host() : nullptr;
    if (host)
        host->dispatch_action("toggle_ui_panels");
}

void GuiActionHandler::change_font_size(float new_size)
{
    PERF_MEASURE();
    new_size = std::clamp(new_size, TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    if (new_size == deps_.text_service->point_size())
        return;
    if (!deps_.text_service->set_point_size(new_size))
        return;

    if (deps_.imgui_host)
        deps_.imgui_host->rebuild_imgui_font_texture();

    if (deps_.on_font_changed)
        deps_.on_font_changed();

    if (deps_.config)
    {
        deps_.config->font_size = new_size;
        if (deps_.on_config_changed)
            deps_.on_config_changed();
    }
}

} // namespace draxul

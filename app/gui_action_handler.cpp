#include "gui_action_handler.h"

#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
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
        {"toggle_diagnostics", [](auto& h) { h.toggle_diagnostics(); }},
        {"copy",               [](auto& h) { h.copy(); }},
        {"paste",              [](auto& h) { h.paste(); }},
        {"font_increase",      [](auto& h) { h.font_increase(); }},
        {"font_decrease",      [](auto& h) { h.font_decrease(); }},
        {"font_reset",         [](auto& h) { h.font_reset(); }},
        {"open_file_dialog",   [](auto& h) { h.open_file_dialog(); }},
        {"split_vertical",     [](auto& h) { h.split_vertical(); }},
        {"split_horizontal",   [](auto& h) { h.split_horizontal(); }},
    };
    // clang-format on
    return map;
}

bool GuiActionHandler::execute(std::string_view action)
{
    const auto& map = action_map();
    if (const auto it = map.find(action); it != map.end())
    {
        it->second(*this);
        return true;
    }
    DRAXUL_LOG_WARN(LogCategory::App, "Unknown GUI action: '%.*s'",
        static_cast<int>(action.size()), action.data());
    return false;
}

void GuiActionHandler::font_increase()
{
    change_font_size(deps_.text_service->point_size() + 0.5f);
}

void GuiActionHandler::font_decrease()
{
    change_font_size(deps_.text_service->point_size() - 0.5f);
}

void GuiActionHandler::font_reset()
{
    change_font_size(TextService::DEFAULT_POINT_SIZE);
}

void GuiActionHandler::copy() const
{
    IHost* host = deps_.focused_host ? deps_.focused_host() : nullptr;
    if (host)
        host->dispatch_action("copy");
}

void GuiActionHandler::paste() const
{
    IHost* host = deps_.focused_host ? deps_.focused_host() : nullptr;
    if (host)
        host->dispatch_action("paste");
}

void GuiActionHandler::toggle_diagnostics() const
{
    deps_.ui_panel->toggle_visible();
    if (deps_.on_panel_toggled)
        deps_.on_panel_toggled();
}

void GuiActionHandler::open_file_dialog() const
{
    if (deps_.on_open_file_dialog)
        deps_.on_open_file_dialog();
}

void GuiActionHandler::split_vertical() const
{
    if (deps_.on_split_vertical)
        deps_.on_split_vertical();
}

void GuiActionHandler::split_horizontal() const
{
    if (deps_.on_split_horizontal)
        deps_.on_split_horizontal();
}

void GuiActionHandler::change_font_size(float new_size)
{
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

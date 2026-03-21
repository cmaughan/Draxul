#include "gui_action_handler.h"

#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/ui_panel.h>

namespace draxul
{

GuiActionHandler::GuiActionHandler(Deps deps)
    : deps_(std::move(deps))
{
}

bool GuiActionHandler::execute(std::string_view action)
{
    if (action == "toggle_diagnostics")
    {
        toggle_diagnostics();
        return true;
    }
    if (action == "copy")
    {
        copy();
        return true;
    }
    if (action == "paste")
    {
        paste();
        return true;
    }
    if (action == "font_increase")
    {
        font_increase();
        return true;
    }
    if (action == "font_decrease")
    {
        font_decrease();
        return true;
    }
    if (action == "font_reset")
    {
        font_reset();
        return true;
    }
    if (action == "open_file_dialog")
    {
        open_file_dialog();
        return true;
    }
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

void GuiActionHandler::copy()
{
    if (deps_.host)
        deps_.host->dispatch_action("copy");
}

void GuiActionHandler::paste()
{
    if (deps_.host)
        deps_.host->dispatch_action("paste");
}

void GuiActionHandler::toggle_diagnostics()
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

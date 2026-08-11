#pragma once

#include <draxul/types.h>

namespace draxul
{

struct ChromeTheme
{
    Color tab_bar_bg = color_from_rgba(0x161616ff);
    Color tab_active_fg = color_from_rgba(0xf5e0dcff);
    Color tab_inactive_fg = color_from_rgba(0xcdd6f4ff);
    Color space_active_bg = color_from_rgba(0x89b4fadc);
    Color agent_active_bg = color_from_rgba(0xcba6f7dc);
    Color tab_active_bg = color_from_rgba(0xb93c3cdc);
    Color tab_inactive_bg = color_from_rgba(0x45475aff);
    Color tab_editing_bg = color_from_rgba(0x8c90afff);
    Color divider = color_from_rgba(0x78788cdc);
    Color focus_border = color_from_rgba(0x7b2828dc);
    Color status_bar_bg = color_from_rgba(0x45475aff);
    Color status_bar_fg = color_from_rgba(0xcdd6f4ff);
    Color status_focused_accent_bg = color_from_rgba(0x3ca55fdc);
    Color status_inactive_accent_bg = color_from_rgba(0x6e738cc8);
    Color status_editing_bg = color_from_rgba(0x8c90afff);
    Color resource_pill_bg = color_from_rgba(0xf9e2afff);
    Color resource_pill_fg = color_from_rgba(0x1a1a1fff);
    Color resource_pill_warn_bg = color_from_rgba(0xf5c282ff);
    Color resource_pill_hot_bg = color_from_rgba(0xf45656ff);
    Color chord_pill_bg = color_from_rgba(0x45475af2);
    Color weather_pill_bg = color_from_rgba(0x474d61ff);
    Color editing_outline = color_from_rgba(0xffffffe6);
};

} // namespace draxul

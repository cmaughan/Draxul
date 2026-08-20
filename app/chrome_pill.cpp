#include "chrome_pill.h"

#include <algorithm>
#include <utility>

namespace draxul
{
namespace
{
constexpr float kFamilyBodyBrightness = 0.30f;

Color faded_family_color(Color color)
{
    color.r *= kFamilyBodyBrightness;
    color.g *= kFamilyBodyBrightness;
    color.b *= kFamilyBodyBrightness;
    return color;
}
} // namespace

ChromePillLayout layout_chrome_pill(ChromePillSpec spec)
{
    ChromePillLayout pill;
    pill.columns = std::max(0, spec.columns);
    pill.text_col = spec.text_col;
    pill.prefix_cols = std::max(0, spec.prefix_cols);
    pill.label = std::move(spec.label);
    pill.palette = spec.palette;

    const float trim = static_cast<float>(std::max(0, spec.cell_width)) * 0.25f;
    pill.rect = {
        spec.grid_x + spec.left_inset + trim,
        spec.grid_y + 2.0f,
        std::max(0.0f, static_cast<float>(pill.columns * spec.cell_width) - trim * 2.0f),
        static_cast<float>(chrome_pill_height(spec.cell_height))
    };
    pill.clip = pill.rect;
    pill.accent_w = spec.accent_fills_pill
        ? pill.rect.w
        : static_cast<float>((1 + pill.prefix_cols) * std::max(0, spec.cell_width));
    return pill;
}

int chrome_pill_height(int cell_height)
{
    return std::max(0, cell_height - 4);
}

int chrome_pill_band_height(int cell_height)
{
    return std::max(0, cell_height);
}

Color chrome_pill_text_color(const Color& background)
{
    constexpr Color kDarkInk{ 0.10f, 0.10f, 0.12f, 1.0f };
    constexpr Color kLightInk{ 0.92f, 0.93f, 0.95f, 1.0f };
    const float luminance =
        0.2126f * background.r + 0.7152f * background.g + 0.0722f * background.b;
    return luminance > 0.5f ? kDarkInk : kLightInk;
}

ChromePillPalette chrome_pill_palette(const ChromeTheme& theme,
    ChromePillRole role, bool emphasized, bool editing)
{
    ChromePillPalette palette;
    Color family_accent = theme.tab_active_bg;
    switch (role)
    {
    case ChromePillRole::Space:
        family_accent = theme.space_active_bg;
        palette.body_fg = emphasized ? theme.tab_active_fg : theme.tab_inactive_fg;
        break;
    case ChromePillRole::Agent:
        family_accent = theme.agent_active_bg;
        palette.body_fg = emphasized ? theme.tab_active_fg : theme.tab_inactive_fg;
        break;
    case ChromePillRole::Tab:
        family_accent = theme.tab_active_bg;
        palette.body_fg = editing
            ? chrome_pill_text_color(theme.tab_editing_bg)
            : (emphasized ? theme.tab_active_fg : theme.tab_inactive_fg);
        break;
    case ChromePillRole::Pane:
        family_accent = theme.status_focused_accent_bg;
        palette.body_fg = editing
            ? chrome_pill_text_color(theme.status_editing_bg)
            : theme.status_bar_fg;
        break;
    case ChromePillRole::PaneAttention:
        family_accent = theme.resource_pill_hot_bg;
        palette.body_fg = chrome_pill_text_color(faded_family_color(family_accent));
        break;
    }

    const Color faded = faded_family_color(family_accent);
    palette.body_bg = editing
        ? (role == ChromePillRole::Tab
                  ? theme.tab_editing_bg
                  : theme.status_editing_bg)
        : faded;
    palette.accent_bg = emphasized ? family_accent : faded;
    palette.accent_fg = chrome_pill_text_color(palette.accent_bg);
    return palette;
}

} // namespace draxul

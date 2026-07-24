#pragma once

#include <draxul/chrome_theme.h>
#include <string>

namespace draxul
{

struct ChromeRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

enum class ChromePillRole
{
    Space,
    Agent,
    Tab,
    Pane,
};

struct ChromePillPalette
{
    Color body_bg{};
    Color accent_bg{};
    Color body_fg{};
    Color accent_fg{};
};

struct ChromePillLayout
{
    int columns = 0;
    int text_col = 1;
    int prefix_cols = 0;
    std::string label;
    ChromeRect rect{};
    ChromeRect clip{};
    float accent_w = 0.0f;
    ChromePillPalette palette{};
};

struct ChromePillSpec
{
    float grid_x = 0.0f;
    float grid_y = 0.0f;
    int columns = 0;
    int text_col = 1;
    int prefix_cols = 0;
    int cell_width = 0;
    int cell_height = 0;
    float left_inset = 0.0f;
    bool accent_fills_pill = false;
    std::string label;
    ChromePillPalette palette{};
};

ChromePillLayout layout_chrome_pill(ChromePillSpec spec);
int chrome_pill_height(int cell_height);
int chrome_pill_band_height(int cell_height);
Color chrome_pill_text_color(const Color& background);
ChromePillPalette chrome_pill_palette(const ChromeTheme& theme,
    ChromePillRole role, bool emphasized, bool editing = false);

} // namespace draxul

#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <array>
#include <vector>

namespace draxul
{

namespace
{

Color ansi_color(int index)
{
    static const std::array<Color, 16> palette = { {
        { 0.05f, 0.06f, 0.07f, 1.0f },
        { 0.80f, 0.24f, 0.24f, 1.0f },
        { 0.40f, 0.73f, 0.42f, 1.0f },
        { 0.88f, 0.73f, 0.30f, 1.0f },
        { 0.29f, 0.51f, 0.82f, 1.0f },
        { 0.70f, 0.41f, 0.78f, 1.0f },
        { 0.28f, 0.73f, 0.80f, 1.0f },
        { 0.84f, 0.84f, 0.85f, 1.0f },
        { 0.33f, 0.34f, 0.35f, 1.0f },
        { 0.94f, 0.38f, 0.38f, 1.0f },
        { 0.49f, 0.82f, 0.54f, 1.0f },
        { 0.96f, 0.82f, 0.44f, 1.0f },
        { 0.46f, 0.65f, 0.93f, 1.0f },
        { 0.81f, 0.55f, 0.88f, 1.0f },
        { 0.48f, 0.86f, 0.93f, 1.0f },
        { 0.97f, 0.98f, 0.98f, 1.0f },
    } };
    return palette[std::clamp(index, 0, 15)];
}

Color xterm_color(int index)
{
    if (index < 16)
        return ansi_color(index);

    if (index <= 231)
    {
        const int value = index - 16;
        const int r = value / 36;
        const int g = (value / 6) % 6;
        const int b = value % 6;
        auto scale = [](int n) {
            static constexpr std::array<int, 6> values = { 0, 95, 135, 175, 215, 255 };
            return static_cast<float>(values[n]) / 255.0f;
        };
        return { scale(r), scale(g), scale(b), 1.0f };
    }

    const auto gray = static_cast<float>((8 + (index - 232) * 10) / 255.0);
    return { gray, gray, gray, 1.0f };
}

void set_color(HlAttr& attr, bool is_fg, Color color)
{
    if (is_fg)
    {
        attr.fg = color;
        attr.has_fg = true;
    }
    else
    {
        attr.bg = color;
        attr.has_bg = true;
    }
}

// Returns the number of extra parameter slots consumed (0 if not recognised).
size_t try_apply_extended_color(HlAttr& attr, bool is_fg, const std::vector<int>& values, size_t i)
{
    if (i + 1 >= values.size())
        return 0;
    if (values[i + 1] == 5 && i + 2 < values.size())
    {
        set_color(attr, is_fg, xterm_color(std::clamp(values[i + 2], 0, 255)));
        return 2;
    }
    if (values[i + 1] == 2 && i + 4 < values.size())
    {
        const Color color = {
            std::clamp(values[i + 2], 0, 255) / 255.0f,
            std::clamp(values[i + 3], 0, 255) / 255.0f,
            std::clamp(values[i + 4], 0, 255) / 255.0f,
            1.0f,
        };
        set_color(attr, is_fg, color);
        return 4;
    }
    return 0;
}

} // namespace

void apply_sgr(HlAttr& current_attr, const std::vector<int>& params)
{
    const std::vector<int> values = params.empty() ? std::vector<int>{ 0 } : params;
    for (size_t i = 0; i < values.size(); ++i)
    {
        const int value = values[i];
        if (value == 0)
            current_attr = {};
        else if (value == 1)
            current_attr.bold = true;
        else if (value == 3)
            current_attr.italic = true;
        else if (value == 4)
            current_attr.underline = true;
        else if (value == 7)
            current_attr.reverse = true;
        else if (value == 9)
            current_attr.strikethrough = true;
        else if (value == 22)
            current_attr.bold = false;
        else if (value == 23)
            current_attr.italic = false;
        else if (value == 24)
            current_attr.underline = false;
        else if (value == 27)
            current_attr.reverse = false;
        else if (value == 29)
            current_attr.strikethrough = false;
        else if (value == 39)
            current_attr.has_fg = false;
        else if (value == 49)
            current_attr.has_bg = false;
        else if (value >= 30 && value <= 37)
            set_color(current_attr, true, ansi_color(value - 30));
        else if (value >= 40 && value <= 47)
            set_color(current_attr, false, ansi_color(value - 40));
        else if (value >= 90 && value <= 97)
            set_color(current_attr, true, ansi_color(value - 90 + 8));
        else if (value >= 100 && value <= 107)
            set_color(current_attr, false, ansi_color(value - 100 + 8));
        else if (value == 38 || value == 48)
            i += try_apply_extended_color(current_attr, value == 38, values, i);
    }
}

} // namespace draxul

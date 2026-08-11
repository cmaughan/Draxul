#include <draxul/terminal_sgr.h>

#include <draxul/perf_timing.h>

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
        Color(0.05f, 0.06f, 0.07f, 1.0f),
        Color(0.80f, 0.24f, 0.24f, 1.0f),
        Color(0.40f, 0.73f, 0.42f, 1.0f),
        Color(0.88f, 0.73f, 0.30f, 1.0f),
        Color(0.29f, 0.51f, 0.82f, 1.0f),
        Color(0.70f, 0.41f, 0.78f, 1.0f),
        Color(0.28f, 0.73f, 0.80f, 1.0f),
        Color(0.84f, 0.84f, 0.85f, 1.0f),
        Color(0.33f, 0.34f, 0.35f, 1.0f),
        Color(0.94f, 0.38f, 0.38f, 1.0f),
        Color(0.49f, 0.82f, 0.54f, 1.0f),
        Color(0.96f, 0.82f, 0.44f, 1.0f),
        Color(0.46f, 0.65f, 0.93f, 1.0f),
        Color(0.81f, 0.55f, 0.88f, 1.0f),
        Color(0.48f, 0.86f, 0.93f, 1.0f),
        Color(0.97f, 0.98f, 0.98f, 1.0f),
    } };
    return palette[std::clamp(index, 0, 15)];
}

Color xterm_color(int index)
{
    PERF_MEASURE();
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
        return Color(scale(r), scale(g), scale(b), 1.0f);
    }

    const auto gray = static_cast<float>((8 + (index - 232) * 10) / 255.0);
    return Color(gray, gray, gray, 1.0f);
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

bool apply_basic_sgr(HlAttr& attr, int value)
{
    PERF_MEASURE();
    switch (value)
    {
    case 0:
        attr = {};
        return true;
    case 1:
        attr.bold = true;
        return true;
    case 3:
        attr.italic = true;
        return true;
    case 4:
        attr.underline = true;
        return true;
    case 7:
        attr.reverse = true;
        return true;
    case 9:
        attr.strikethrough = true;
        return true;
    case 22:
        attr.bold = false;
        return true;
    case 23:
        attr.italic = false;
        return true;
    case 24:
        attr.underline = false;
        return true;
    case 27:
        attr.reverse = false;
        return true;
    case 29:
        attr.strikethrough = false;
        return true;
    case 39:
        attr.has_fg = false;
        return true;
    case 49:
        attr.has_bg = false;
        return true;
    default:
        return false;
    }
}

// Returns the number of extra parameter slots consumed (0 if not recognised).
size_t try_apply_extended_color(HlAttr& attr, bool is_fg, const std::vector<int>& values, size_t i)
{
    PERF_MEASURE();
    if (i + 1 >= values.size())
        return 0;
    if (values[i + 1] == 5 && i + 2 < values.size())
    {
        set_color(attr, is_fg, xterm_color(std::clamp(values[i + 2], 0, 255)));
        return 2;
    }
    if (values[i + 1] == 2 && i + 4 < values.size())
    {
        const Color color(
            static_cast<float>(std::clamp(values[i + 2], 0, 255)) / 255.0f,
            static_cast<float>(std::clamp(values[i + 3], 0, 255)) / 255.0f,
            static_cast<float>(std::clamp(values[i + 4], 0, 255)) / 255.0f,
            1.0f);
        set_color(attr, is_fg, color);
        return 4;
    }
    return 0;
}

} // namespace

void apply_sgr(HlAttr& current_attr, const std::vector<int>& params)
{
    PERF_MEASURE();
    const std::vector<int> values = params.empty() ? std::vector<int>{ 0 } : params;
    for (size_t i = 0; i < values.size(); ++i)
    {
        const int value = values[i];
        if (apply_basic_sgr(current_attr, value))
            continue;
        if (value >= 30 && value <= 37)
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

#include <draxul/markdown/markdown_theme.h>

#include <algorithm>

namespace draxul::markdown
{

namespace
{

MarkdownTextStyle make_style(float point_size, bool bold, draxul::Color foreground)
{
    MarkdownTextStyle style;
    style.rich_text.point_size = point_size;
    style.rich_text.bold = bold;
    style.foreground = foreground;
    return style;
}

} // namespace

MarkdownTheme default_markdown_theme(float base_point_size)
{
    const float body_size = std::max(base_point_size, 1.0f);
    const auto body_fg = draxul::Color(0.86f, 0.89f, 0.93f, 1.0f);
    const auto heading_fg = draxul::Color(0.96f, 0.98f, 1.0f, 1.0f);
    const auto muted_heading_fg = draxul::Color(0.88f, 0.93f, 0.98f, 1.0f);
    const auto code_fg = draxul::Color(0.78f, 0.88f, 0.74f, 1.0f);

    MarkdownTheme theme;
    theme.body = make_style(body_size, false, body_fg);
    theme.heading1 = make_style(body_size * 1.40f, true, heading_fg);
    theme.heading2 = make_style(body_size * 1.20f, true, heading_fg);
    theme.heading3 = make_style(body_size * 1.06f, true, muted_heading_fg);
    theme.heading4 = make_style(body_size, true, muted_heading_fg);
    theme.heading5 = make_style(body_size, true, muted_heading_fg);
    theme.heading6 = make_style(body_size, true, muted_heading_fg);
    theme.code = make_style(body_size, false, code_fg);
    theme.code.background = theme.code_background;
    return theme;
}

const MarkdownTextStyle& style_for_heading(const MarkdownTheme& theme, int level)
{
    switch (level)
    {
    case 1:
        return theme.heading1;
    case 2:
        return theme.heading2;
    case 3:
        return theme.heading3;
    case 4:
        return theme.heading4;
    case 5:
        return theme.heading5;
    case 6:
        return theme.heading6;
    default:
        return theme.heading6;
    }
}

} // namespace draxul::markdown

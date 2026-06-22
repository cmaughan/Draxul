#pragma once

#include <draxul/rich_text_service.h>
#include <draxul/types.h>

namespace draxul::markdown
{

struct MarkdownTextStyle
{
    draxul::RichTextStyleKey rich_text;
    draxul::Color foreground = draxul::Color(0.88f, 0.91f, 0.95f, 1.0f);
    draxul::Color background = draxul::Color(0.0f, 0.0f, 0.0f, 0.0f);
    float line_height_multiplier = 1.25f;
};

struct MarkdownTheme
{
    MarkdownTextStyle body;
    MarkdownTextStyle heading1;
    MarkdownTextStyle heading2;
    MarkdownTextStyle heading3;
    MarkdownTextStyle heading4;
    MarkdownTextStyle heading5;
    MarkdownTextStyle heading6;
    MarkdownTextStyle code;

    draxul::Color document_background = draxul::Color(0.06f, 0.07f, 0.09f, 1.0f);
    draxul::Color panel_background = draxul::Color(0.09f, 0.11f, 0.14f, 1.0f);
    draxul::Color code_background = draxul::Color(0.08f, 0.10f, 0.13f, 1.0f);
    draxul::Color border = draxul::Color(0.23f, 0.29f, 0.37f, 1.0f);
    draxul::Color accent = draxul::Color(0.38f, 0.64f, 0.96f, 1.0f);
};

MarkdownTheme default_markdown_theme(float base_point_size);
const MarkdownTextStyle& style_for_heading(const MarkdownTheme& theme, int level);

} // namespace draxul::markdown

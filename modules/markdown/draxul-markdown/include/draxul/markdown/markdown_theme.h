#pragma once

#include <draxul/rich_text_service.h>
#include <draxul/types.h>

#include <cstdint>

namespace draxul::markdown
{

// A style id packs a base style (body/headings/code) in its low bits with the
// inline emphasis picked up from `**bold**` / `*italic*` spans in the high bits,
// so a single row can mix emphasis without needing a theme entry per combination.
struct StyleId
{
    uint16_t value = 0;
};

inline constexpr uint16_t kStyleBaseMask = 0x000F;
inline constexpr uint16_t kStyleBoldFlag = 0x0010;
inline constexpr uint16_t kStyleItalicFlag = 0x0020;

inline constexpr StyleId kBodyStyle{ 0 };
inline constexpr StyleId kHeading1Style{ 1 };
inline constexpr StyleId kHeading2Style{ 2 };
inline constexpr StyleId kHeading3Style{ 3 };
inline constexpr StyleId kHeading4Style{ 4 };
inline constexpr StyleId kHeading5Style{ 5 };
inline constexpr StyleId kHeading6Style{ 6 };
inline constexpr StyleId kCodeStyle{ 7 };

constexpr StyleId base_style_of(StyleId style)
{
    return StyleId{ static_cast<uint16_t>(style.value & kStyleBaseMask) };
}

constexpr StyleId with_bold(StyleId style)
{
    return StyleId{ static_cast<uint16_t>(style.value | kStyleBoldFlag) };
}

constexpr StyleId with_italic(StyleId style)
{
    return StyleId{ static_cast<uint16_t>(style.value | kStyleItalicFlag) };
}

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

// Resolves a style id to a concrete style, folding the emphasis flags into the
// rich-text key. Returned by value because emphasis variants are derived, not stored.
MarkdownTextStyle resolve_markdown_style(const MarkdownTheme& theme, StyleId style);

} // namespace draxul::markdown

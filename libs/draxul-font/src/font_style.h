#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace draxul
{

// Indexed model for the four font style variants. Regular is the primary
// face; the other three are optional variants resolved from explicit config
// paths or filename auto-detection. Count exists so containers can be
// indexed by style (std::array<T, FONT_STYLE_COUNT>).
enum class FontStyle : uint8_t
{
    Regular = 0,
    Bold,
    Italic,
    BoldItalic,
    Count,
};

inline constexpr size_t FONT_STYLE_COUNT = static_cast<size_t>(FontStyle::Count);

constexpr size_t font_style_index(FontStyle style)
{
    return static_cast<size_t>(style);
}

constexpr bool font_style_is_bold(FontStyle style)
{
    return style == FontStyle::Bold || style == FontStyle::BoldItalic;
}

constexpr bool font_style_is_italic(FontStyle style)
{
    return style == FontStyle::Italic || style == FontStyle::BoldItalic;
}

constexpr FontStyle font_style_from_flags(bool is_bold, bool is_italic)
{
    if (is_bold && is_italic)
        return FontStyle::BoldItalic;
    if (is_bold)
        return FontStyle::Bold;
    if (is_italic)
        return FontStyle::Italic;
    return FontStyle::Regular;
}

// The optional style variants, in load/diagnostic order (matches the order
// warnings are reported in FontResolver::initialize()).
inline constexpr std::array<FontStyle, 3> FONT_STYLE_VARIANTS = {
    FontStyle::Bold,
    FontStyle::Italic,
    FontStyle::BoldItalic,
};

// Capitalized name used in load diagnostics ("Bold font not found: ...").
constexpr const char* font_style_display_name(FontStyle style)
{
    switch (style)
    {
    case FontStyle::Bold:
        return "Bold";
    case FontStyle::Italic:
        return "Italic";
    case FontStyle::BoldItalic:
        return "Bold-italic";
    default:
        return "Regular";
    }
}

// Lowercase name used in "No <style> font variant found" diagnostics.
constexpr const char* font_style_lower_name(FontStyle style)
{
    switch (style)
    {
    case FontStyle::Bold:
        return "bold";
    case FontStyle::Italic:
        return "italic";
    case FontStyle::BoldItalic:
        return "bold-italic";
    default:
        return "regular";
    }
}

// Filename token substituted for "Regular" during variant auto-detection
// ("JetBrainsMonoNerdFont-Regular.ttf" -> "JetBrainsMonoNerdFont-BoldItalic.ttf").
constexpr const char* font_style_file_suffix(FontStyle style)
{
    switch (style)
    {
    case FontStyle::Bold:
        return "Bold";
    case FontStyle::Italic:
        return "Italic";
    case FontStyle::BoldItalic:
        return "BoldItalic";
    default:
        return "Regular";
    }
}

// Style-variant preference order used during selection: the requested style
// first, then progressively less specific variants. Regular is deliberately
// not part of any chain — callers fall back to the primary face when no
// variant in the chain is loaded or can render the cluster.
//   BoldItalic -> Bold -> Italic
//   Bold       -> Bold
//   Italic     -> Italic
//   Regular    -> (empty)
struct FontStyleChain
{
    std::array<FontStyle, 3> styles = {};
    size_t count = 0;

    constexpr const FontStyle* begin() const
    {
        return styles.data();
    }
    constexpr const FontStyle* end() const
    {
        return styles.data() + count;
    }
    constexpr size_t size() const
    {
        return count;
    }
};

constexpr FontStyleChain font_style_fallback_chain(FontStyle style)
{
    switch (style)
    {
    case FontStyle::BoldItalic:
        return { { FontStyle::BoldItalic, FontStyle::Bold, FontStyle::Italic }, 3 };
    case FontStyle::Bold:
        return { { FontStyle::Bold }, 1 };
    case FontStyle::Italic:
        return { { FontStyle::Italic }, 1 };
    default:
        return {};
    }
}

} // namespace draxul

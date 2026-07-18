#pragma once

#include "font_engine.h"
#include "font_style.h"

#include <array>
#include <string>
#include <vector>

namespace draxul
{

struct TextServiceConfig;

namespace detail
{

std::vector<std::string> default_fallback_font_candidates();
std::vector<std::string> default_primary_font_candidates();
std::string first_existing_path(const std::vector<std::string>& candidates);
// Derives a style-variant path from the regular path by filename convention
// ("...-Regular.ttf" -> "...-Bold.ttf" etc.); empty when no candidate file exists.
std::string auto_detect_style_path(const std::string& regular_path, FontStyle style);

} // namespace detail

// Discovers and loads the primary font face, its optional style variants
// (bold/italic/bold-italic), and all fallback faces.
class FontResolver
{
public:
    // One loaded face plus its shapers. Used for all four style slots and
    // for the fallback list (only fallbacks use the lazy-load `failed` flag;
    // style variants are loaded eagerly during initialize()).
    struct StyledFont
    {
        FontManager font;
        TextShaper shaper;
        TextShaper unligated_shaper;
        std::string path;
        bool loaded = false;
        bool failed = false;
    };
    using FallbackFont = StyledFont;

    // Initializes the primary font, style variants, and all fallback fonts.
    bool initialize(const TextServiceConfig& config, float point_size, float display_ppi);

    void shutdown();

    // Resizes all fonts; returns false if primary resize fails.
    // Unloaded fallbacks are skipped — they will initialize at the new size
    // when first needed, since ensure_loaded() uses primary().point_size().
    bool set_point_size(float point_size);

    // Style-indexed access. Regular is the primary face and is always loaded
    // after a successful initialize(); the other styles are optional.
    StyledFont& style(FontStyle style)
    {
        return styles_[font_style_index(style)];
    }
    const StyledFont& style(FontStyle style) const
    {
        return styles_[font_style_index(style)];
    }
    bool has_style(FontStyle style) const
    {
        return styles_[font_style_index(style)].loaded;
    }

    FontManager& primary()
    {
        return style(FontStyle::Regular).font;
    }
    const FontManager& primary() const
    {
        return style(FontStyle::Regular).font;
    }
    TextShaper& primary_shaper()
    {
        return style(FontStyle::Regular).shaper;
    }
    TextShaper& primary_unligated_shaper()
    {
        return style(FontStyle::Regular).unligated_shaper;
    }

    std::vector<FallbackFont>& fallbacks()
    {
        return fallbacks_;
    }
    const std::vector<FallbackFont>& fallbacks() const
    {
        return fallbacks_;
    }

    const std::string& font_path() const
    {
        return style(FontStyle::Regular).path;
    }

    // Ensure the fallback at `index` is loaded. Returns false if it failed to
    // load (permanently — subsequent calls also return false without retrying).
    bool ensure_loaded(size_t index);

    void load_fallback_fonts();

    FontManager& bold()
    {
        return style(FontStyle::Bold).font;
    }
    TextShaper& bold_shaper()
    {
        return style(FontStyle::Bold).shaper;
    }
    TextShaper& bold_unligated_shaper()
    {
        return style(FontStyle::Bold).unligated_shaper;
    }
    bool has_bold() const
    {
        return has_style(FontStyle::Bold);
    }
    const std::string& bold_font_path() const
    {
        return style(FontStyle::Bold).path;
    }

    FontManager& italic()
    {
        return style(FontStyle::Italic).font;
    }
    TextShaper& italic_shaper()
    {
        return style(FontStyle::Italic).shaper;
    }
    TextShaper& italic_unligated_shaper()
    {
        return style(FontStyle::Italic).unligated_shaper;
    }
    bool has_italic() const
    {
        return has_style(FontStyle::Italic);
    }
    const std::string& italic_font_path() const
    {
        return style(FontStyle::Italic).path;
    }

    FontManager& bold_italic()
    {
        return style(FontStyle::BoldItalic).font;
    }
    TextShaper& bold_italic_shaper()
    {
        return style(FontStyle::BoldItalic).shaper;
    }
    TextShaper& bold_italic_unligated_shaper()
    {
        return style(FontStyle::BoldItalic).unligated_shaper;
    }
    bool has_bold_italic() const
    {
        return has_style(FontStyle::BoldItalic);
    }
    const std::string& bold_italic_font_path() const
    {
        return style(FontStyle::BoldItalic).path;
    }

    // Warnings collected during initialize() — e.g. missing bold/italic/bold-italic
    // variants. The list is filled once per initialize() and consumed by callers
    // (typically TextService → App) which surface them as toast notifications.
    std::vector<std::string> take_warnings()
    {
        return std::move(warnings_);
    }

private:
    // Loads one optional style variant, preserving the per-style diagnostics
    // (path from config first, then filename auto-detection).
    void load_style_variant(FontStyle variant, float point_size);

    const TextServiceConfig* config_ = nullptr;
    float display_ppi_ = 96.0f;
    std::array<StyledFont, FONT_STYLE_COUNT> styles_;
    std::vector<FallbackFont> fallbacks_;
    std::vector<std::string> warnings_;
};

} // namespace draxul

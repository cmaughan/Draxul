#pragma once

#include "font_engine.h"
#include "font_resolver.h"
#include "font_style.h"

#include <draxul/text_service.h>
#include <draxul/unicode.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

namespace detail
{

inline bool can_render_cluster(FT_Face face, TextShaper& shaper, const std::string& text)
{
    if (!face)
        return false;

    auto shaped = shaper.shape(text);
    if (shaped.empty())
        return false;

    bool has_glyph = false;
    for (const auto& glyph : shaped)
    {
        if (glyph.glyph_id == 0)
            return false;
        if (FT_Load_Glyph(face, glyph.glyph_id, FT_LOAD_DEFAULT))
            return false;
        has_glyph = true;
    }

    return has_glyph;
}

inline bool cluster_prefers_color_font(std::string_view text)
{
    size_t offset = 0;
    uint32_t base = 0;
    bool have_base = false;
    bool has_vs16 = false;
    bool has_zwj = false;
    bool has_emoji_modifier = false;
    bool has_keycap = false;

    while (offset < text.size())
    {
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        if (cp == 0xFE0F)
            has_vs16 = true;
        else if (cp == 0x200D)
            has_zwj = true;
        else if (cp == 0x20E3)
            has_keycap = true;
        else if (is_emoji_modifier(cp))
            has_emoji_modifier = true;

        if (!have_base && !is_width_ignorable(cp) && !is_emoji_modifier(cp))
        {
            base = cp;
            have_base = true;
        }
    }

    if (!have_base)
        return false;

    if (is_default_emoji_presentation(base) || is_regional_indicator(base))
        return true;

    if ((has_vs16 || has_zwj || has_emoji_modifier) && is_emoji_text_presentation_candidate(base))
        return true;

    if (has_keycap && is_ascii_keycap_base(base))
        return true;

    return false;
}

inline bool font_has_color(FT_Face face)
{
    return face && FT_HAS_COLOR(face);
}

struct TransparentStringHash
{
    using is_transparent = void;

    size_t operator()(std::string_view text) const noexcept
    {
        return std::hash<std::string_view>{}(text);
    }
};

struct TransparentStringEqual
{
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }
};

} // namespace detail

// Selects the best font face for a given text cluster, with per-cluster
// caching. Each FontStyle keeps its own cache slot so a cluster can resolve
// to different faces per style.
class FontSelector
{
public:
    struct Selection
    {
        FT_Face face = nullptr;
        TextShaper* shaper = nullptr;
    };

    void reset_cache()
    {
        for (auto& cache : caches_)
            cache.clear();
    }

    void set_cache_limit(size_t limit)
    {
        cache_limit_ = std::max<size_t>(1, limit);
    }

    // Returns the face+shaper best suited to render `text`. Styled requests
    // walk the style's fallback chain (BoldItalic -> Bold -> Italic) before
    // degrading to the regular selection path.
    Selection select(const std::string& text, FontResolver& resolver, bool is_bold = false, bool is_italic = false)
    {
        for (FontStyle variant : font_style_fallback_chain(font_style_from_flags(is_bold, is_italic)))
        {
            if (!resolver.has_style(variant))
                continue;
            auto sel = try_variant_selection(variant, text, resolver);
            if (sel.face)
                return sel;
        }

        auto& regular_cache = cache_for(FontStyle::Regular);
        auto it = regular_cache.find(text);
        if (it != regular_cache.end())
        {
            int idx = it->second;
            if (idx < 0)
                return { resolver.primary().face(), &resolver.primary_shaper() };
            auto& fallbacks = resolver.fallbacks();
            if (idx < (int)fallbacks.size())
                return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
        }

        if (detail::cluster_prefers_color_font(text))
        {
            if (auto sel = select_color_font(text, resolver))
                return *sel;
        }

        if (detail::can_render_cluster(resolver.primary().face(), resolver.primary_shaper(), text))
        {
            store(FontStyle::Regular, text, -1);
            return { resolver.primary().face(), &resolver.primary_shaper() };
        }

        auto fb = search_fallbacks(text, resolver, /*color_only=*/false);
        if (fb.face)
            return fb;

        store(FontStyle::Regular, text, -1);
        return { resolver.primary().face(), &resolver.primary_shaper() };
    }

    size_t cache_size() const
    {
        return cache_size(FontStyle::Regular);
    }

    size_t cache_size(FontStyle style) const
    {
        return caches_[font_style_index(style)].size();
    }

    // Rasterization can fail after coverage selection succeeds (for example,
    // a malformed bitmap in one face). Find another face without retrying a
    // face that already failed during this resolve operation.
    Selection select_after_raster_failure(
        const std::string& text, FontResolver& resolver, const std::vector<FT_Face>& excluded_faces)
    {
        const auto excluded = [&excluded_faces](FT_Face face) {
            return std::find(excluded_faces.begin(), excluded_faces.end(), face) != excluded_faces.end();
        };

        auto& fallbacks = resolver.fallbacks();
        for (int i = 0; i < static_cast<int>(fallbacks.size()); ++i)
        {
            if (!resolver.ensure_loaded(static_cast<size_t>(i)))
                continue;
            auto& fallback = fallbacks[static_cast<size_t>(i)];
            if (excluded(fallback.font.face()))
                continue;
            if (detail::can_render_cluster(fallback.font.face(), fallback.shaper, text))
            {
                store(FontStyle::Regular, text, i);
                return { fallback.font.face(), &fallback.shaper };
            }
        }

        if (!excluded(resolver.primary().face())
            && detail::can_render_cluster(resolver.primary().face(), resolver.primary_shaper(), text))
        {
            store(FontStyle::Regular, text, -1);
            return { resolver.primary().face(), &resolver.primary_shaper() };
        }
        return {};
    }

private:
    using StyleCache = std::unordered_map<std::string, int, detail::TransparentStringHash, detail::TransparentStringEqual>;

    StyleCache& cache_for(FontStyle style)
    {
        return caches_[font_style_index(style)];
    }

    // Search fallback fonts for a renderable cluster. If color_only, only color-capable fonts are tried.
    Selection search_fallbacks(const std::string& text, FontResolver& resolver, bool color_only)
    {
        auto& fallbacks = resolver.fallbacks();
        for (int i = 0; i < (int)fallbacks.size(); i++)
        {
            if (!resolver.ensure_loaded((size_t)i))
                continue;
            auto& fb = fallbacks[(size_t)i];
            if (color_only && !detail::font_has_color(fb.font.face()))
                continue;
            if (detail::can_render_cluster(fb.font.face(), fb.shaper, text))
            {
                store(FontStyle::Regular, text, i);
                return { fb.font.face(), &fb.shaper };
            }
        }
        return {};
    }

    // Try primary color font then color fallbacks for emoji/color clusters.
    std::optional<Selection> select_color_font(const std::string& text, FontResolver& resolver)
    {
        if (detail::font_has_color(resolver.primary().face())
            && detail::can_render_cluster(resolver.primary().face(), resolver.primary_shaper(), text))
        {
            store(FontStyle::Regular, text, -1);
            return Selection{ resolver.primary().face(), &resolver.primary_shaper() };
        }
        auto fb = search_fallbacks(text, resolver, /*color_only=*/true);
        if (fb.face)
            return fb;
        return std::nullopt;
    }

    // Check the style's cache then try its variant face; returns an empty
    // Selection on miss so the caller can degrade along the fallback chain.
    Selection try_variant_selection(FontStyle style, const std::string& text, FontResolver& resolver)
    {
        auto& slot = resolver.style(style);
        FT_Face variant_face = slot.font.face();
        TextShaper& variant_shaper = slot.shaper;

        auto& variant_cache = cache_for(style);
        auto it = variant_cache.find(text);
        if (it != variant_cache.end())
        {
            if (it->second < 0)
                return { variant_face, &variant_shaper };
            auto& fallbacks = resolver.fallbacks();
            int idx = it->second;
            if (idx < (int)fallbacks.size())
                return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
        }
        if (detail::can_render_cluster(variant_face, variant_shaper, text))
        {
            store(style, text, -1);
            return { variant_face, &variant_shaper };
        }
        return {};
    }

    void store(FontStyle style, const std::string& text, int idx)
    {
        auto& cache = cache_for(style);
        if (cache.size() >= cache_limit_)
            cache.clear();
        cache[text] = idx;
    }

    std::array<StyleCache, FONT_STYLE_COUNT> caches_;
    size_t cache_limit_ = TextServiceConfig::DEFAULT_FONT_CHOICE_CACHE_LIMIT;
};

} // namespace draxul

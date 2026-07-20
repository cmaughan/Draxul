#pragma once

#include "font_engine.h"
#include "font_resolver.h"
#include "font_selector.h"
#include "font_style.h"

#include <draxul/unicode.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

namespace
{

inline int count_codepoints(std::string_view text)
{
    int count = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            return 0;
        ++count;
    }
    return count;
}

inline bool shaping_differs(
    const std::vector<ShapedGlyph>& lhs, const std::vector<ShapedGlyph>& rhs)
{
    if (lhs.size() != rhs.size())
        return true;

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i].glyph_id != rhs[i].glyph_id || lhs[i].x_advance != rhs[i].x_advance
            || lhs[i].x_offset != rhs[i].x_offset || lhs[i].y_offset != rhs[i].y_offset
            || lhs[i].cluster != rhs[i].cluster)
        {
            return true;
        }
    }

    return false;
}

// Per-cell glyph signature: which glyphs (and at what offsets) a single
// codepoint cell receives from a shaping run.
struct CellGlyphSignature
{
    std::vector<ShapedGlyph> glyphs;

    bool operator==(const CellGlyphSignature& other) const
    {
        return !shaping_differs(glyphs, other.glyphs);
    }
};

// Groups shaped glyphs by the codepoint cell their cluster maps to.
inline std::vector<CellGlyphSignature> group_glyphs_by_cell(
    const std::vector<ShapedGlyph>& shaped, const std::vector<int>& cp_indices, int cp_count)
{
    std::vector<CellGlyphSignature> cells((size_t)std::max(cp_count, 0));
    const size_t max_byte = cp_indices.empty() ? 0 : cp_indices.size() - 1;
    for (const auto& glyph : shaped)
    {
        const size_t byte = std::min((size_t)std::max(glyph.cluster, 0), max_byte);
        const int cell = cp_indices.empty() ? 0 : cp_indices[byte];
        if (cell >= 0 && cell < cp_count)
            cells[(size_t)cell].glyphs.push_back(glyph);
    }
    return cells;
}

} // namespace

// Detects eligible ligature spans in a cluster sequence.
class LigatureAnalyser
{
public:
    void reset_cache()
    {
        cache_.clear();
    }

    void set_cache_limit(size_t limit)
    {
        cache_limit_ = std::max<size_t>(1, limit);
    }

    // Maximum number of cells a ligature can span.
    static constexpr int MAX_LIGATURE_CELLS = 6;

    // Returns the ligature span width in cells (0 = no ligature, 2+ = multi-cell ligature).
    // Requires ligatures enabled; otherwise always returns 0.
    int ligature_cell_span(
        const std::string& text, bool enable_ligatures, FontSelector& selector, FontResolver& resolver,
        bool is_bold = false, bool is_italic = false)
    {
        if (!enable_ligatures)
            return 0;

        auto it = cache_.find(text);
        if (it != cache_.end())
            return it->second;

        const int codepoint_count = count_codepoints(text);
        if (codepoint_count < 2)
        {
            store(text, 0);
            return 0;
        }

        auto sel = selector.select(text, resolver, is_bold, is_italic);
        auto shaped = sel.shaper->shape(text);
        if (shaped.empty())
        {
            store(text, 0);
            return 0;
        }

        // Find the unligated shaper for the chosen face: style variants
        // first, then fallback faces (faces are distinct per slot).
        TextShaper* unligated = &resolver.primary_unligated_shaper();
        bool matched_variant = false;
        for (FontStyle variant : FONT_STYLE_VARIANTS)
        {
            if (resolver.has_style(variant) && sel.face == resolver.style(variant).font.face())
            {
                unligated = &resolver.style(variant).unligated_shaper;
                matched_variant = true;
                break;
            }
        }
        if (!matched_variant && sel.face != resolver.primary().face())
        {
            auto& fallbacks = resolver.fallbacks();
            auto fb_it = std::find_if(
                fallbacks.begin(), fallbacks.end(),
                [&](const FontResolver::FallbackFont& fb) { return fb.font.face() == sel.face; });
            if (fb_it != fallbacks.end())
                unligated = &fb_it->unligated_shaper;
        }

        auto plain = unligated->shape(text);
        if (plain.empty() || !shaping_differs(shaped, plain))
        {
            store(text, 0);
            return 0;
        }

        // Shaping differs somewhere, but only the contiguous run of cells that
        // actually changed — starting at the leader — forms the ligature.
        // Without this, any candidate containing a ligature pair anywhere
        // (e.g. "--rend" while typing "--render-test") would be grouped into a
        // single cluster, displacing the unrelated trailing glyphs.
        const auto cp_indices = utf8_codepoint_indices(text);
        const auto shaped_cells = group_glyphs_by_cell(shaped, cp_indices, codepoint_count);
        const auto plain_cells = group_glyphs_by_cell(plain, cp_indices, codepoint_count);

        int diff_run = 0;
        while (diff_run < codepoint_count
            && !(shaped_cells[(size_t)diff_run] == plain_cells[(size_t)diff_run]))
            ++diff_run;

        const int ligature_span = (diff_run >= 2 && diff_run <= MAX_LIGATURE_CELLS) ? diff_run : 0;
        store(text, ligature_span);
        return ligature_span;
    }

    size_t cache_size() const
    {
        return cache_.size();
    }

private:
    void store(const std::string& text, int span)
    {
        if (cache_.size() >= cache_limit_)
            cache_.clear();
        cache_[text] = span;
    }

    std::unordered_map<std::string, int> cache_;
    size_t cache_limit_ = TextServiceConfig::DEFAULT_FONT_CHOICE_CACHE_LIMIT;
};

} // namespace draxul

#pragma once

#include <draxul/glyph_atlas.h>
#include <draxul/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace draxul::tests
{

class FakeGlyphAtlas final : public IGlyphAtlas
{
public:
    explicit FakeGlyphAtlas(int resets_remaining = 0)
        : resets_remaining_(resets_remaining)
    {
        atlas_.assign(16, 0x7F);
        register_glyph("A", { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false });
        register_glyph("B", { { 0.25f, 0.0f, 0.5f, 0.5f }, { 2, 3 }, { 8, 10 }, false });
        register_glyph("-", { { 0.5f, 0.0f, 0.625f, 0.5f }, { 1, 1 }, { 6, 3 }, false });
        register_glyph(">", { { 0.625f, 0.0f, 0.75f, 0.5f }, { 1, 1 }, { 6, 8 }, false });
        register_glyph("X", { { 0.75f, 0.0f, 0.875f, 0.5f }, { 1, 2 }, { 7, 9 }, false });
        register_glyph("->", { { 0.0f, 0.5f, 0.375f, 1.0f }, { 1, 2 }, { 18, 9 }, false });
        set_ligature_span("->", 2);
    }

    AtlasRegion resolve_cluster(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        ++resolve_calls;
        resolved_texts.push_back(text);
        atlas_dirty_ = true;

        auto it = glyphs_.find(text);
        if (it != glyphs_.end())
            return it->second;
        return {};
    }

    int ligature_cell_span(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        auto it = ligature_spans_.find(text);
        return it != ligature_spans_.end() ? it->second : 0;
    }

    bool atlas_dirty() const override
    {
        return atlas_dirty_;
    }

    bool consume_atlas_reset() override
    {
        if (resets_remaining_ <= 0)
            return false;

        --resets_remaining_;
        atlas_dirty_ = false;
        return true;
    }

    void clear_atlas_dirty() override
    {
        atlas_dirty_ = false;
    }

    const uint8_t* atlas_data() const override
    {
        return atlas_.data();
    }

    int atlas_width() const override
    {
        return 2;
    }

    int atlas_height() const override
    {
        return 2;
    }

    AtlasDirtyRect atlas_dirty_rect() const override
    {
        return { { 0, 0 }, { 2, 2 } };
    }

    void register_glyph(const std::string& text, AtlasRegion region)
    {
        glyphs_[text] = region;
    }

    void set_ligature_span(const std::string& text, int span)
    {
        ligature_spans_[text] = span;
    }

    int resolve_calls = 0;
    std::vector<std::string> resolved_texts;

private:
    int resets_remaining_ = 0;
    bool atlas_dirty_ = false;
    std::vector<uint8_t> atlas_;
    std::unordered_map<std::string, AtlasRegion> glyphs_;
    std::unordered_map<std::string, int> ligature_spans_;
};

} // namespace draxul::tests

#pragma once

#include "box_drawing.h"
#include "font_engine.h"
#include "font_resolver.h"
#include "font_selector.h"

#include <array>
#include <cassert>
#include <draxul/log.h>
#include <vector>

namespace draxul
{

// Manages atlas packing, rasterisation, and atlas-overflow reset policy.
class GlyphAtlasManager
{
public:
    bool initialize(FT_Face primary_face, int point_size)
    {
        atlas_reset_pending_ = false;
        atlas_reset_count_ = 0;
        warned_error_kinds_.fill(false);
        runtime_warnings_.clear();
        expected_primary_face_ = primary_face;
        return glyph_cache_.initialize(primary_face, point_size);
    }

    void reset_atlas(FT_Face primary_face, int point_size)
    {
        expected_primary_face_ = primary_face;
        glyph_cache_.reset(primary_face, point_size);
        atlas_reset_pending_ = true;
        atlas_reset_count_++;
    }

    // Resolves a text cluster to an atlas region, retrying once after an overflow reset.
    AtlasRegion resolve_cluster(
        const std::string& text, FontSelector& selector, FontResolver& resolver,
        bool is_bold = false, bool is_italic = false)
    {
        // Safety: verify the primary face hasn't changed without a reset_atlas() call.
        // This catches use-after-free if TextService recreates the FT_Face without
        // resetting the glyph cache.
        assert(expected_primary_face_ == resolver.primary().face()
            && "GlyphAtlasManager: primary FT_Face changed without reset_atlas()");
        if (expected_primary_face_ != resolver.primary().face())
        {
            DRAXUL_LOG_WARN(LogCategory::Font,
                "GlyphAtlasManager: primary face changed without reset; auto-resetting (gen=%u)",
                glyph_cache_.face_generation());
            reset_atlas(resolver.primary().face(), static_cast<int>(resolver.primary().point_size()));
        }

        // Box drawing / block elements are synthesized at exact cell size so
        // they tile without seams; font selection is bypassed entirely.
        if (const uint32_t box_cp = synthesized_box_codepoint(text))
        {
            const FontMetrics& metrics = resolver.primary().metrics();
            AtlasRegion region = glyph_cache_.get_box_glyph(
                box_cp, text, metrics.cell_width, metrics.cell_height, metrics.ascender);
            if (region.bitmap_size.x > 0 || !glyph_cache_.consume_overflowed())
                return region;

            reset_after_overflow(resolver);
            return glyph_cache_.get_box_glyph(
                box_cp, text, metrics.cell_width, metrics.cell_height, metrics.ascender);
        }

        auto sel = selector.select(text, resolver, is_bold, is_italic);
        std::vector<FT_Face> failed_faces;
        while (sel.face != nullptr && sel.shaper != nullptr)
        {
            auto result = glyph_cache_.get_cluster_result(text, sel.face, *sel.shaper);
            if (result.has_value())
                return result.value();

            if (result.error().kind == ErrorKind::AtlasOverflow)
            {
                reset_after_overflow(resolver);
                sel = selector.select(text, resolver, is_bold, is_italic);
                auto retry = glyph_cache_.get_cluster_result(text, sel.face, *sel.shaper);
                if (retry.has_value())
                    return retry.value();
                warn_raster_failure_once(retry.error());
                return {};
            }

            failed_faces.push_back(sel.face);
            sel = selector.select_after_raster_failure(text, resolver, failed_faces);
            if (sel.face == nullptr)
            {
                warn_raster_failure_once(result.error());
                return {};
            }
        }
        return {};
    }

    GlyphCache& cache()
    {
        return glyph_cache_;
    }
    const GlyphCache& cache() const
    {
        return glyph_cache_;
    }

    bool consume_atlas_reset()
    {
        bool pending = atlas_reset_pending_;
        atlas_reset_pending_ = false;
        return pending;
    }

    int reset_count() const
    {
        return atlas_reset_count_;
    }

    std::vector<std::string> take_runtime_warnings()
    {
        std::vector<std::string> warnings;
        warnings.swap(runtime_warnings_);
        return warnings;
    }

private:
    void warn_raster_failure_once(const Error& error)
    {
        const size_t index = static_cast<size_t>(error.kind);
        if (index >= warned_error_kinds_.size() || warned_error_kinds_[index])
            return;
        warned_error_kinds_[index] = true;
        DRAXUL_LOG_WARN(LogCategory::Font, "Glyph rendering failed after fallback exhaustion: %s",
            error.message.c_str());
        runtime_warnings_.push_back(
            "A glyph could not be rendered by any available font: " + error.message);
    }

    // Atlas overflowed — reset so the caller can retry once.
    void reset_after_overflow(FontResolver& resolver)
    {
        glyph_cache_.reset(resolver.primary().face(), static_cast<int>(resolver.primary().point_size()));
        atlas_reset_pending_ = true;
        atlas_reset_count_++;
        DRAXUL_LOG_WARN(
            LogCategory::Font, "Glyph atlas reset after exhaustion (count=%d)", atlas_reset_count_);
    }

    GlyphCache glyph_cache_;
    FT_Face expected_primary_face_ = nullptr;
    bool atlas_reset_pending_ = false;
    int atlas_reset_count_ = 0;
    std::array<bool, static_cast<size_t>(ErrorKind::NotFound) + 1> warned_error_kinds_ = {};
    std::vector<std::string> runtime_warnings_;
};

} // namespace draxul

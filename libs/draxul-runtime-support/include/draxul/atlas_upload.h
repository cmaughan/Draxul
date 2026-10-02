#pragma once

#include <draxul/glyph_atlas.h>
#include <draxul/renderer.h>
#include <draxul/pending_atlas_upload.h>
#include <draxul/log.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace draxul
{

/// Upload any dirty atlas region to the renderer and clear the dirty flag.
/// Call exactly once per frame before begin_frame() so all subsystems that
/// resolved glyphs in the previous frame see their atlas pixels uploaded
/// before rendering begins.
///
/// @param atlas       The shared glyph atlas that owns dirty state.
/// @param renderer    The grid renderer that owns the GPU atlas texture.
/// @param scratch     Reusable scratch buffer to avoid per-frame allocation.
inline void upload_atlas_dirty_region(
    IGlyphAtlas& atlas, IGridRenderer& renderer, std::vector<uint8_t>& scratch)
{
    if (!atlas.atlas_dirty())
        return;

    const auto dirty = atlas.atlas_dirty_rect();
    if (dirty.size.x <= 0 || dirty.size.y <= 0)
    {
        atlas.clear_atlas_dirty();
        return;
    }

    constexpr size_t kPixelSize = 4;
    const uint8_t* atlas_data = atlas.atlas_data();
    const int atlas_w = atlas.atlas_width();
    if (!atlas_data || !atlas_upload_rect_in_bounds(dirty.pos.x, dirty.pos.y,
            dirty.size.x, dirty.size.y, atlas_w, atlas.atlas_height())
        || atlas_upload_size_bytes(atlas_w, atlas.atlas_height()) == 0)
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer, "Rejected invalid dirty glyph atlas region");
        atlas.clear_atlas_dirty();
        return;
    }
    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
    scratch.resize(row_bytes * static_cast<size_t>(dirty.size.y));

    for (int r = 0; r < dirty.size.y; ++r)
    {
        const uint8_t* src = atlas_data
            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
    }

    renderer.update_atlas_region(
        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
    atlas.clear_atlas_dirty();
}

} // namespace draxul

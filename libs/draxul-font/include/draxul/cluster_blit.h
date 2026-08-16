#pragma once

#include <draxul/unicode.h>

#include <cstdint>
#include <functional>
#include <span>

namespace draxul
{

class TextService;

// Writes one destination RGBA8 pixel. `dst` points at the 4-byte pixel,
// `coverage` is the glyph alpha sampled from the shared glyph atlas (never
// zero), and dst_x/dst_y are the (already bounds-checked) destination
// coordinates for callers that track ink extents.
using ClusterPixelWriter
    = std::function<void(uint8_t* dst, uint8_t coverage, int dst_x, int dst_y)>;

// Blits a run of display clusters onto a tightly packed RGBA8 surface,
// clipping to the surface bounds. The pen advances by
// cell_width * cluster.cell_width, so wide (CJK/emoji) clusters occupy two
// cells instead of overlapping the following glyph. Returns the pen x
// position after the final cluster.
int blit_cluster_run_rgba(TextService& text_service,
    std::span<const DisplayCluster> clusters,
    int pen_x, int baseline_y,
    uint8_t* dst_pixels, int dst_width, int dst_height,
    const ClusterPixelWriter& write_pixel);

} // namespace draxul

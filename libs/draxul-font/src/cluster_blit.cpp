#include <draxul/cluster_blit.h>

#include <draxul/text_service.h>

#include <algorithm>
#include <cmath>

namespace draxul
{

int blit_cluster_run_rgba(TextService& text_service,
    std::span<const DisplayCluster> clusters,
    int pen_x, int baseline_y,
    uint8_t* dst_pixels, int dst_width, int dst_height,
    const ClusterPixelWriter& write_pixel)
{
    const FontMetrics metrics = text_service.metrics();
    const int cell_width = std::max(metrics.cell_width, 1);

    for (const DisplayCluster& cluster : clusters)
    {
        const AtlasRegion region = text_service.resolve_cluster(cluster.text);
        const int advance = cell_width * std::max(cluster.cell_width, 1);
        if (region.bitmap_size.x <= 0 || region.bitmap_size.y <= 0)
        {
            pen_x += advance;
            continue;
        }

        const uint8_t* atlas = text_service.atlas_data();
        const int atlas_width = text_service.atlas_width();
        const int atlas_height = text_service.atlas_height();
        if (!atlas || atlas_width <= 0 || atlas_height <= 0)
        {
            pen_x += advance;
            continue;
        }

        const int src_x0 = std::clamp(
            static_cast<int>(std::lround(region.uv.x * atlas_width)), 0, atlas_width - 1);
        const int src_y0 = std::clamp(
            static_cast<int>(std::lround(region.uv.y * atlas_height)), 0, atlas_height - 1);
        const int dst_x0 = pen_x + region.bitmap_bearing.x;
        const int dst_y0 = baseline_y - region.bitmap_bearing.y;

        for (int row = 0; row < region.bitmap_size.y; ++row)
        {
            const int dst_y = dst_y0 + row;
            const int src_y = src_y0 + row;
            if (dst_y < 0 || dst_y >= dst_height || src_y < 0 || src_y >= atlas_height)
                continue;

            for (int col = 0; col < region.bitmap_size.x; ++col)
            {
                const int dst_x = dst_x0 + col;
                const int src_x = src_x0 + col;
                if (dst_x < 0 || dst_x >= dst_width || src_x < 0 || src_x >= atlas_width)
                    continue;

                const uint8_t* src = atlas + (((src_y * atlas_width) + src_x) * 4);
                if (src[3] == 0)
                    continue;

                uint8_t* dst = dst_pixels + (((dst_y * dst_width) + dst_x) * 4);
                write_pixel(dst, src[3], dst_x, dst_y);
            }
        }

        pen_x += advance;
    }

    return pen_x;
}

} // namespace draxul

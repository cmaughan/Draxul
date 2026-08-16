#include <draxul/text_atlas_builder.h>

#include <algorithm>
#include <cstring>
#include <draxul/cluster_blit.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>
#include <numeric>
#include <string_view>
#include <utility>

namespace draxul
{

namespace
{

constexpr int kAtlasPadding = 2;
constexpr int kInitialAtlasWidth = 512;
constexpr int kMaxAtlasDimension = 8192;

struct LabelBitmap
{
    int width = 1;
    int height = 1;
    std::vector<uint8_t> rgba{ 0, 0, 0, 0 };
    glm::ivec2 ink_pixel_size{ 0 };
};

struct PackedLabel
{
    std::string key;
    LabelBitmap bitmap;
    int x = 0;
    int y = 0;
};

int cluster_run_cells(const std::vector<DisplayCluster>& clusters)
{
    return std::accumulate(clusters.begin(), clusters.end(), 0,
        [](int cells, const DisplayCluster& cluster) {
            return cells + std::max(cluster.cell_width, 1);
        });
}

std::vector<DisplayCluster> fit_clusters(
    TextService& text_service,
    std::string_view text,
    int available_width)
{
    std::vector<DisplayCluster> clusters = display_clusters(text);
    if (clusters.empty() || available_width <= 0)
        return {};

    const int advance = std::max(text_service.metrics().cell_width, 1);
    if (cluster_run_cells(clusters) * advance <= available_width)
        return clusters;

    constexpr int kEllipsisCells = 3;
    const int max_cells = std::max(1, available_width / advance);
    const int keep_cells = max_cells <= kEllipsisCells
        ? max_cells
        : max_cells - kEllipsisCells;

    std::vector<DisplayCluster> fitted;
    int cells = 0;
    for (DisplayCluster& cluster : clusters)
    {
        const int width = std::max(cluster.cell_width, 1);
        if (cells + width > keep_cells)
            break;
        cells += width;
        fitted.push_back(std::move(cluster));
    }
    if (max_cells > kEllipsisCells)
    {
        for (int i = 0; i < kEllipsisCells; ++i)
            fitted.push_back(DisplayCluster{ .text = ".", .cell_width = 1 });
    }
    return fitted;
}

void write_colored_pixel(
    uint8_t* dst,
    uint8_t source_alpha,
    const std::array<uint8_t, 4>& color)
{
    const uint8_t alpha = static_cast<uint8_t>(
        (static_cast<uint16_t>(source_alpha) * static_cast<uint16_t>(color[3]) + 127u) / 255u);
    dst[0] = color[0];
    dst[1] = color[1];
    dst[2] = color[2];
    dst[3] = std::max(dst[3], alpha);
}

LabelBitmap rasterize_label(TextService& text_service, const TextAtlasRequest& request)
{
    LabelBitmap bitmap;
    bitmap.width = std::max(1, request.target_pixel_size.x);
    bitmap.height = std::max(1, request.target_pixel_size.y);
    bitmap.rgba.assign(static_cast<size_t>(bitmap.width * bitmap.height * 4), 0);

    const int padding = std::max(0, request.padding);
    const int available_width = std::max(0, bitmap.width - padding * 2);
    const std::vector<DisplayCluster> clusters = fit_clusters(text_service, request.text, available_width);
    if (clusters.empty())
        return bitmap;

    const FontMetrics metrics = text_service.metrics();
    const int advance = std::max(metrics.cell_width, 1);
    const int estimated_text_width = cluster_run_cells(clusters) * advance;
    int pen_x = padding;
    if (request.horizontal_align == TextAtlasHorizontalAlign::Center
        && bitmap.width > estimated_text_width + padding * 2)
    {
        pen_x = (bitmap.width - estimated_text_width) / 2;
    }
    const int baseline_y = request.vertical_align == TextAtlasVerticalAlign::Top
        ? std::max(0, request.top_padding) + metrics.ascender
        : (bitmap.height - metrics.cell_height) / 2 + metrics.ascender;

    int ink_min_x = bitmap.width;
    int ink_min_y = bitmap.height;
    int ink_max_x = -1;
    int ink_max_y = -1;
    blit_cluster_run_rgba(text_service, clusters, pen_x, baseline_y,
        bitmap.rgba.data(), bitmap.width, bitmap.height,
        [&](uint8_t* dst, uint8_t coverage, int dst_x, int dst_y) {
            write_colored_pixel(dst, coverage, request.color);
            ink_min_x = std::min(ink_min_x, dst_x);
            ink_min_y = std::min(ink_min_y, dst_y);
            ink_max_x = std::max(ink_max_x, dst_x);
            ink_max_y = std::max(ink_max_y, dst_y);
        });

    if (ink_max_x >= ink_min_x && ink_max_y >= ink_min_y)
    {
        bitmap.ink_pixel_size = {
            ink_max_x - ink_min_x + 1,
            ink_max_y - ink_min_y + 1,
        };
    }
    return bitmap;
}

bool pack_labels(
    std::vector<PackedLabel>& labels,
    int atlas_width,
    int max_height,
    int& atlas_height)
{
    int x = kAtlasPadding;
    int y = kAtlasPadding;
    int row_height = 0;
    bool all_fit = true;
    for (PackedLabel& label : labels)
    {
        if (label.bitmap.width + kAtlasPadding * 2 > atlas_width)
        {
            label.x = -1;
            all_fit = false;
            continue;
        }
        if (x + label.bitmap.width + kAtlasPadding > atlas_width)
        {
            const int next_y = y + row_height + kAtlasPadding;
            if (next_y + label.bitmap.height + kAtlasPadding > max_height)
            {
                label.x = -1;
                all_fit = false;
                continue;
            }
            x = kAtlasPadding;
            y = next_y;
            row_height = 0;
        }
        label.x = x;
        label.y = y;
        x += label.bitmap.width + kAtlasPadding;
        row_height = std::max(row_height, label.bitmap.height);
    }
    atlas_height = y + row_height + kAtlasPadding;
    return all_fit;
}

} // namespace

TextAtlas build_text_atlas(
    TextService& text_service,
    std::span<const TextAtlasRequest> requests,
    uint64_t revision)
{
    TextAtlas atlas;
    atlas.image.width = 1;
    atlas.image.height = 1;
    atlas.image.revision = revision;
    atlas.image.rgba = { 0, 0, 0, 0 };

    std::vector<TextAtlasRequest> filtered(requests.begin(), requests.end());
    filtered.erase(
        std::remove_if(filtered.begin(), filtered.end(), [](const TextAtlasRequest& request) {
            return request.key.empty() || request.text.empty()
                || request.target_pixel_size.x + kAtlasPadding * 2 > kMaxAtlasDimension
                || request.target_pixel_size.y + kAtlasPadding * 2 > kMaxAtlasDimension;
        }),
        filtered.end());
    std::stable_sort(filtered.begin(), filtered.end(), [](const TextAtlasRequest& lhs, const TextAtlasRequest& rhs) {
        return lhs.key < rhs.key;
    });
    filtered.erase(
        std::unique(filtered.begin(), filtered.end(), [](const TextAtlasRequest& lhs, const TextAtlasRequest& rhs) {
            return lhs.key == rhs.key;
        }),
        filtered.end());
    if (filtered.empty())
        return atlas;

    std::vector<PackedLabel> packed;
    packed.reserve(filtered.size());
    for (const TextAtlasRequest& request : filtered)
        packed.push_back({ request.key, rasterize_label(text_service, request), 0, 0 });

    int atlas_width = kInitialAtlasWidth;
    int atlas_height = 1;
    while (atlas_width < kMaxAtlasDimension
        && !pack_labels(packed, atlas_width, kMaxAtlasDimension, atlas_height))
    {
        atlas_width *= 2;
    }
    if (atlas_width >= kMaxAtlasDimension)
    {
        atlas_width = kMaxAtlasDimension;
        pack_labels(packed, atlas_width, kMaxAtlasDimension, atlas_height);
    }

    atlas.image.width = atlas_width;
    atlas.image.height = std::max(1, atlas_height);
    atlas.image.rgba.assign(
        static_cast<size_t>(atlas.image.width) * static_cast<size_t>(atlas.image.height) * 4u,
        0);
    const float inv_width = 1.0f / static_cast<float>(atlas.image.width);
    const float inv_height = 1.0f / static_cast<float>(atlas.image.height);
    for (const PackedLabel& label : packed)
    {
        if (label.x < 0)
            continue;
        for (int row = 0; row < label.bitmap.height; ++row)
        {
            const uint8_t* src = label.bitmap.rgba.data()
                + static_cast<size_t>(row * label.bitmap.width) * 4u;
            uint8_t* dst = atlas.image.rgba.data()
                + static_cast<size_t>(((label.y + row) * atlas.image.width) + label.x) * 4u;
            std::memcpy(dst, src, static_cast<size_t>(label.bitmap.width) * 4u);
        }
        atlas.entries.emplace(label.key,
            TextAtlasEntry{
                {
                    static_cast<float>(label.x) * inv_width,
                    static_cast<float>(label.y) * inv_height,
                    static_cast<float>(label.x + label.bitmap.width) * inv_width,
                    static_cast<float>(label.y + label.bitmap.height) * inv_height,
                },
                { label.bitmap.width, label.bitmap.height },
                {
                    label.bitmap.ink_pixel_size.x > 0 ? label.bitmap.ink_pixel_size.x : label.bitmap.width,
                    label.bitmap.ink_pixel_size.y > 0 ? label.bitmap.ink_pixel_size.y : label.bitmap.height,
                },
            });
    }
    return atlas;
}

} // namespace draxul

#include <draxul/text_atlas_builder.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/text_service.h>
#include <draxul/unicode.h>
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

std::vector<std::string> split_clusters(std::string_view text)
{
    std::vector<std::string> clusters;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t begin = offset;
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        bool previous_was_zwj = cp == 0x200D;
        while (offset < text.size())
        {
            size_t next = offset;
            uint32_t next_cp = 0;
            if (!utf8_decode_next(text, next, next_cp))
                break;
            const bool joins = previous_was_zwj || next_cp == 0x200D
                || is_width_ignorable(next_cp) || is_emoji_modifier(next_cp);
            if (!joins)
                break;
            previous_was_zwj = next_cp == 0x200D;
            offset = next;
        }
        clusters.emplace_back(text.substr(begin, offset - begin));
    }
    return clusters;
}

std::vector<std::string> fit_clusters(
    TextService& text_service,
    std::string_view text,
    int available_width)
{
    std::vector<std::string> clusters = split_clusters(text);
    if (clusters.empty() || available_width <= 0)
        return {};

    const int advance = std::max(text_service.metrics().cell_width, 1);
    if (static_cast<int>(clusters.size()) * advance <= available_width)
        return clusters;

    const std::vector<std::string> ellipsis = { ".", ".", "." };
    const int max_clusters = std::max(1, available_width / advance);
    if (max_clusters <= static_cast<int>(ellipsis.size()))
    {
        clusters.resize(static_cast<size_t>(std::min(max_clusters, static_cast<int>(clusters.size()))));
        return clusters;
    }

    clusters.resize(static_cast<size_t>(max_clusters - static_cast<int>(ellipsis.size())));
    clusters.insert(clusters.end(), ellipsis.begin(), ellipsis.end());
    return clusters;
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
    const std::vector<std::string> clusters =
        fit_clusters(text_service, request.text, available_width);
    if (clusters.empty())
        return bitmap;

    const FontMetrics metrics = text_service.metrics();
    const int advance = std::max(metrics.cell_width, 1);
    const int estimated_text_width = static_cast<int>(clusters.size()) * advance;
    int pen_x = padding;
    if (request.horizontal_align == TextAtlasHorizontalAlign::Center
        && bitmap.width > estimated_text_width + padding * 2)
    {
        pen_x = (bitmap.width - estimated_text_width) / 2;
    }

    int ink_min_x = bitmap.width;
    int ink_min_y = bitmap.height;
    int ink_max_x = -1;
    int ink_max_y = -1;
    for (const std::string& cluster : clusters)
    {
        const AtlasRegion region = text_service.resolve_cluster(cluster);
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
        const int baseline_y = request.vertical_align == TextAtlasVerticalAlign::Top
            ? std::max(0, request.top_padding) + metrics.ascender
            : (bitmap.height - metrics.cell_height) / 2 + metrics.ascender;
        const int dst_y0 = baseline_y - region.bitmap_bearing.y;

        for (int row = 0; row < region.bitmap_size.y; ++row)
        {
            const int dst_y = dst_y0 + row;
            const int src_y = src_y0 + row;
            if (dst_y < 0 || dst_y >= bitmap.height || src_y < 0 || src_y >= atlas_height)
                continue;

            for (int col = 0; col < region.bitmap_size.x; ++col)
            {
                const int dst_x = dst_x0 + col;
                const int src_x = src_x0 + col;
                if (dst_x < 0 || dst_x >= bitmap.width || src_x < 0 || src_x >= atlas_width)
                    continue;
                const uint8_t* src = atlas + (((src_y * atlas_width) + src_x) * 4);
                if (src[3] == 0)
                    continue;
                uint8_t* dst = bitmap.rgba.data() + (((dst_y * bitmap.width) + dst_x) * 4);
                write_colored_pixel(dst, src[3], request.color);
                ink_min_x = std::min(ink_min_x, dst_x);
                ink_min_y = std::min(ink_min_y, dst_y);
                ink_max_x = std::max(ink_max_x, dst_x);
                ink_max_y = std::max(ink_max_y, dst_y);
            }
        }
        pen_x += advance;
    }

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

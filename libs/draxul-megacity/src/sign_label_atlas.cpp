#include "sign_label_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace draxul
{

namespace
{

constexpr int kLabelPadding = 2;
constexpr int kTopAlignedLabelPadding = 10;
constexpr int kAtlasPadding = 2;
constexpr int kInitialAtlasWidth = 512;
constexpr int kMaxAtlasDimension = 4096;

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

void write_colored_pixel(uint8_t* dst, uint8_t alpha, uint8_t r, uint8_t g, uint8_t b)
{
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = std::max(dst[3], alpha);
}

std::string elide_label(std::string_view label, int max_pixel_width, int cell_width)
{
    if (label.empty())
        return {};

    const int usable_width = std::max(0, max_pixel_width - kLabelPadding * 2);
    if (usable_width <= 0 || cell_width <= 0)
        return std::string(1, label.front());

    const int max_chars = std::max(1, usable_width / cell_width);
    if (static_cast<int>(label.size()) <= max_chars)
        return std::string(label);

    if (max_chars <= 3)
        return std::string(label.substr(0, static_cast<size_t>(max_chars)));

    return std::string(label.substr(0, static_cast<size_t>(max_chars - 3))) + "...";
}

LabelBitmap rasterize_label(TextService& text_service, const SignLabelRequest& request)
{
    LabelBitmap bitmap;
    bitmap.width = std::max(1, request.target_pixel_width);
    bitmap.height = std::max(1, request.target_pixel_height);
    bitmap.rgba.assign(static_cast<size_t>(bitmap.width * bitmap.height * 4), 0);

    if (request.text.empty())
        return bitmap;

    const FontMetrics metrics = text_service.metrics();
    const int cell_width = std::max(metrics.cell_width, 1);
    const std::string label = elide_label(request.text, bitmap.width, cell_width);
    if (label.empty())
        return bitmap;

    const int estimated_text_width = static_cast<int>(label.size()) * cell_width;
    int pen_x = kLabelPadding;
    if (!request.align_primary_to_start && bitmap.width > estimated_text_width + kLabelPadding * 2)
        pen_x = (bitmap.width - estimated_text_width) / 2;

    int ink_min_x = bitmap.width;
    int ink_min_y = bitmap.height;
    int ink_max_x = -1;
    int ink_max_y = -1;

    for (const unsigned char ch : label)
    {
        const std::string cluster(1, static_cast<char>(ch));
        const AtlasRegion region = text_service.resolve_cluster(cluster);
        if (region.size.x <= 0 || region.size.y <= 0)
        {
            pen_x += cell_width;
            continue;
        }

        const uint8_t* atlas = text_service.atlas_data();
        const int atlas_width = text_service.atlas_width();
        const int atlas_height = text_service.atlas_height();
        if (!atlas || atlas_width <= 0 || atlas_height <= 0)
        {
            pen_x += cell_width;
            continue;
        }

        const int src_x0 = std::clamp(static_cast<int>(std::lround(region.uv.x * atlas_width)), 0, atlas_width - 1);
        const int src_y0 = std::clamp(static_cast<int>(std::lround(region.uv.y * atlas_height)), 0, atlas_height - 1);
        const int dst_x0 = pen_x + region.bearing.x;
        const int baseline_y = request.vertical_align == SignLabelVerticalAlign::Top
            ? kTopAlignedLabelPadding + metrics.ascender
            : (bitmap.height - metrics.cell_height) / 2 + metrics.ascender;
        const int dst_y0 = baseline_y - region.bearing.y;

        for (int row = 0; row < region.size.y; ++row)
        {
            const int dst_y = dst_y0 + row;
            const int src_y = src_y0 + row;
            if (dst_y < 0 || dst_y >= bitmap.height || src_y < 0 || src_y >= atlas_height)
                continue;

            for (int col = 0; col < region.size.x; ++col)
            {
                const int dst_x = dst_x0 + col;
                const int src_x = src_x0 + col;
                if (dst_x < 0 || dst_x >= bitmap.width || src_x < 0 || src_x >= atlas_width)
                    continue;

                const uint8_t* src = atlas + (((src_y * atlas_width) + src_x) * 4);
                if (src[3] == 0)
                    continue;
                uint8_t* dst = bitmap.rgba.data() + (((dst_y * bitmap.width) + dst_x) * 4);
                write_colored_pixel(dst, src[3], request.text_r, request.text_g, request.text_b);
                ink_min_x = std::min(ink_min_x, dst_x);
                ink_min_y = std::min(ink_min_y, dst_y);
                ink_max_x = std::max(ink_max_x, dst_x);
                ink_max_y = std::max(ink_max_y, dst_y);
            }
        }

        pen_x += cell_width;
    }

    if (ink_max_x >= ink_min_x && ink_max_y >= ink_min_y)
    {
        bitmap.ink_pixel_size = glm::ivec2(
            ink_max_x - ink_min_x + 1,
            ink_max_y - ink_min_y + 1);
    }

    return bitmap;
}

bool try_pack_labels(std::vector<PackedLabel>& labels, int atlas_width, int& atlas_height)
{
    int x = kAtlasPadding;
    int y = kAtlasPadding;
    int row_height = 0;

    for (auto& label : labels)
    {
        if (label.bitmap.width + kAtlasPadding * 2 > atlas_width)
            return false;

        if (x + label.bitmap.width + kAtlasPadding > atlas_width)
        {
            x = kAtlasPadding;
            y += row_height + kAtlasPadding;
            row_height = 0;
        }

        label.x = x;
        label.y = y;
        x += label.bitmap.width + kAtlasPadding;
        row_height = std::max(row_height, label.bitmap.height);
    }

    atlas_height = y + row_height + kAtlasPadding;
    return atlas_height <= kMaxAtlasDimension;
}

} // namespace

std::shared_ptr<SignLabelAtlas> build_sign_label_atlas(
    TextService& text_service, const std::vector<SignLabelRequest>& requests, uint64_t revision)
{
    auto atlas = std::make_shared<SignLabelAtlas>();
    atlas->image.width = 1;
    atlas->image.height = 1;
    atlas->image.revision = revision;
    atlas->image.rgba = { 0, 0, 0, 0 };

    std::vector<SignLabelRequest> filtered_requests = requests;
    filtered_requests.erase(
        std::remove_if(filtered_requests.begin(), filtered_requests.end(), [](const SignLabelRequest& request) {
            return request.key.empty() || request.text.empty();
        }),
        filtered_requests.end());
    std::sort(filtered_requests.begin(), filtered_requests.end(), [](const SignLabelRequest& a, const SignLabelRequest& b) {
        return a.key < b.key;
    });
    if (filtered_requests.empty())
        return atlas;

    std::vector<PackedLabel> packed;
    packed.reserve(filtered_requests.size());
    for (const SignLabelRequest& request : filtered_requests)
        packed.push_back({ request.key, rasterize_label(text_service, request), 0, 0 });

    int atlas_width = kInitialAtlasWidth;
    int atlas_height = 1;
    while (atlas_width <= kMaxAtlasDimension && !try_pack_labels(packed, atlas_width, atlas_height))
        atlas_width *= 2;
    if (atlas_width > kMaxAtlasDimension || atlas_height <= 0)
        return atlas;

    const int atlas_size = std::max(atlas_width, atlas_height);
    atlas->image.width = atlas_size;
    atlas->image.height = atlas_size;
    atlas->image.rgba.assign(static_cast<size_t>(atlas_size * atlas_size * 4), 0);

    for (const PackedLabel& label : packed)
    {
        for (int row = 0; row < label.bitmap.height; ++row)
        {
            const uint8_t* src = label.bitmap.rgba.data() + (static_cast<size_t>(row * label.bitmap.width) * 4);
            uint8_t* dst = atlas->image.rgba.data()
                + (static_cast<size_t>(((label.y + row) * atlas_size) + label.x) * 4);
            std::memcpy(dst, src, static_cast<size_t>(label.bitmap.width * 4));
        }

        const float inv_size = 1.0f / static_cast<float>(atlas_size);
        atlas->entries.emplace(label.key,
            SignAtlasEntry{
                {
                    static_cast<float>(label.x) * inv_size,
                    static_cast<float>(label.y) * inv_size,
                    static_cast<float>(label.x + label.bitmap.width) * inv_size,
                    static_cast<float>(label.y + label.bitmap.height) * inv_size,
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

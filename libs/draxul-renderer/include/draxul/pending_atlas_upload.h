#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace draxul
{

struct PendingAtlasUpload
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool full_upload = false;
    std::vector<uint8_t> pixels;
};

inline size_t atlas_upload_size_bytes(int w, int h)
{
    if (w <= 0 || h <= 0)
        return 0;
    const size_t max_bytes = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    if (static_cast<size_t>(w) > max_bytes / 4 / static_cast<size_t>(h))
        return 0;
    return static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
}

inline bool atlas_upload_rect_in_bounds(int x, int y, int w, int h, int atlas_width, int atlas_height)
{
    // Subtract only after checking origins; x+w and y+h may overflow int.
    return atlas_width > 0 && atlas_height > 0 && x >= 0 && y >= 0
        && x < atlas_width && y < atlas_height && w > 0 && h > 0
        && w <= atlas_width - x && h <= atlas_height - y;
}

inline bool validate_pending_atlas_uploads(std::span<const PendingAtlasUpload> uploads,
    int atlas_width, int atlas_height, size_t& total_bytes)
{
    total_bytes = 0;
    const size_t max_bytes = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    for (const auto& upload : uploads)
    {
        const size_t bytes = atlas_upload_size_bytes(upload.w, upload.h);
        if (!atlas_upload_rect_in_bounds(upload.x, upload.y, upload.w, upload.h, atlas_width, atlas_height)
            || bytes == 0 || bytes != upload.pixels.size()
            || bytes > max_bytes - total_bytes)
            return false;
        total_bytes += bytes;
    }
    return true;
}

inline size_t pending_atlas_upload_size_bytes(const std::vector<PendingAtlasUpload>& uploads)
{
    size_t total = 0;
    const size_t max_bytes = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    for (const auto& upload : uploads)
    {
        if (upload.pixels.size() > max_bytes - total)
            return 0;
        total += upload.pixels.size();
    }
    return total;
}

inline bool queue_full_atlas_upload(std::vector<PendingAtlasUpload>& uploads, const uint8_t* data, int w, int h,
    int atlas_width, int atlas_height)
{
    const size_t bytes = atlas_upload_size_bytes(w, h);
    if (bytes == 0 || data == nullptr || !atlas_upload_rect_in_bounds(0, 0, w, h, atlas_width, atlas_height))
        return false;

    PendingAtlasUpload upload;
    upload.w = w;
    upload.h = h;
    upload.full_upload = true;
    upload.pixels.assign(data, data + bytes);

    uploads.clear();
    uploads.push_back(std::move(upload));
    return true;
}

inline bool queue_atlas_region_upload(std::vector<PendingAtlasUpload>& uploads, int x, int y, int w, int h, const uint8_t* data,
    int atlas_width, int atlas_height)
{
    const size_t bytes = atlas_upload_size_bytes(w, h);
    if (bytes == 0 || data == nullptr || !atlas_upload_rect_in_bounds(x, y, w, h, atlas_width, atlas_height))
        return false;

    if (!uploads.empty() && uploads.front().full_upload)
    {
        auto& full_upload = uploads.front();
        if (!atlas_upload_rect_in_bounds(x, y, w, h, full_upload.w, full_upload.h)
            || full_upload.pixels.size() != atlas_upload_size_bytes(full_upload.w, full_upload.h))
            return false;

        for (int row = 0; row < h; ++row)
        {
            const auto* src = data + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
            auto* dst = full_upload.pixels.data()
                + ((static_cast<size_t>(y + row) * static_cast<size_t>(full_upload.w)) + static_cast<size_t>(x)) * 4;
            std::memcpy(dst, src, static_cast<size_t>(w) * 4);
        }
        return true;
    }

    PendingAtlasUpload upload;
    upload.x = x;
    upload.y = y;
    upload.w = w;
    upload.h = h;
    upload.pixels.assign(data, data + bytes);
    uploads.push_back(std::move(upload));
    return true;
}

} // namespace draxul

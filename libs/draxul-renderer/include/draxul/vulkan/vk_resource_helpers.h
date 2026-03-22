#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace draxul
{

enum class BufferResizeResult
{
    Unchanged,
    Resized,
    Failed,
};

template <typename BufferHandle, typename AllocationHandle>
struct OwnedMappedBuffer
{
    BufferHandle buffer{};
    AllocationHandle allocation{};
    void* mapped = nullptr;
    size_t size = 0;
};

template <typename BufferState, typename CreateFn, typename DestroyFn>
BufferResizeResult ensure_buffer_size(BufferState& current, size_t required_size, CreateFn&& create_buffer, DestroyFn&& destroy_buffer)
{
    if (required_size <= current.size)
        return BufferResizeResult::Unchanged;

    BufferState replacement{};
    if (!create_buffer(required_size, replacement))
        return BufferResizeResult::Failed;

    if (current.buffer != BufferState{}.buffer)
        destroy_buffer(current);

    current = std::move(replacement);
    return BufferResizeResult::Resized;
}

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
    return static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
}

inline size_t pending_atlas_upload_size_bytes(const std::vector<PendingAtlasUpload>& uploads)
{
    size_t total = 0;
    for (const auto& upload : uploads)
        total += upload.pixels.size();
    return total;
}

inline void queue_full_atlas_upload(std::vector<PendingAtlasUpload>& uploads, const uint8_t* data, int w, int h)
{
    const size_t bytes = atlas_upload_size_bytes(w, h);
    if (bytes == 0 || data == nullptr)
        return;

    PendingAtlasUpload upload;
    upload.w = w;
    upload.h = h;
    upload.full_upload = true;
    upload.pixels.assign(data, data + bytes);

    uploads.clear();
    uploads.push_back(std::move(upload));
}

inline void queue_atlas_region_upload(std::vector<PendingAtlasUpload>& uploads, int x, int y, int w, int h, const uint8_t* data)
{
    if (!uploads.empty() && uploads.front().full_upload)
    {
        auto& full_upload = uploads.front();
        if (x < 0 || y < 0 || x + w > full_upload.w || y + h > full_upload.h)
            return;

        for (int row = 0; row < h; ++row)
        {
            const auto* src = data + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
            auto* dst = full_upload.pixels.data()
                + ((static_cast<size_t>(y + row) * static_cast<size_t>(full_upload.w)) + static_cast<size_t>(x)) * 4;
            std::memcpy(dst, src, static_cast<size_t>(w) * 4);
        }
        return;
    }

    const size_t bytes = atlas_upload_size_bytes(w, h);
    if (bytes == 0 || data == nullptr)
        return;

    PendingAtlasUpload upload;
    upload.x = x;
    upload.y = y;
    upload.w = w;
    upload.h = h;
    upload.pixels.assign(data, data + bytes);
    uploads.push_back(std::move(upload));
}

} // namespace draxul

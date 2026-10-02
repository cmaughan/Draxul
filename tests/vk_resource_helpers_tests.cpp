
#include <catch2/catch_all.hpp>

#include <draxul/pending_atlas_upload.h>
#include <draxul/vulkan/vk_resource_ownership.h>
#include <array>
#include <limits>

using namespace draxul;

TEST_CASE("vulkan buffer helper preserves the existing buffer on resize failure", "[renderer]")
{
    OwnedMappedBuffer<int, int> buffer;
    buffer.buffer = 11;
    buffer.allocation = 22;
    buffer.mapped = reinterpret_cast<void*>(0x1234);
    buffer.size = 64;

    int destroy_calls = 0;
    const auto result = ensure_buffer_size(buffer, 128, [&](size_t, OwnedMappedBuffer<int, int>&) { return false; }, [&](const OwnedMappedBuffer<int, int>&) { ++destroy_calls; });

    INFO("failed resize reports failure");
    REQUIRE(result == BufferResizeResult::Failed);
    INFO("failed resize preserves buffer handle");
    REQUIRE(buffer.buffer == 11);
    INFO("failed resize preserves allocation handle");
    REQUIRE(buffer.allocation == 22);
    INFO("failed resize preserves size");
    REQUIRE(buffer.size == static_cast<size_t>(64));
    INFO("failed resize does not destroy the old buffer");
    REQUIRE(destroy_calls == 0);
}

TEST_CASE("vulkan buffer helper replaces the old buffer only after a successful allocation", "[renderer]")
{
    OwnedMappedBuffer<int, int> buffer;
    buffer.buffer = 11;
    buffer.allocation = 22;
    buffer.mapped = reinterpret_cast<void*>(0x1234);
    buffer.size = 64;

    OwnedMappedBuffer<int, int> destroyed;
    int destroy_calls = 0;
    const auto result = ensure_buffer_size(buffer, 128, [&](size_t requested_size, OwnedMappedBuffer<int, int>& replacement) {
            replacement.buffer = 33;
            replacement.allocation = 44;
            replacement.mapped = reinterpret_cast<void*>(0x5678);
            replacement.size = requested_size;
            return true; }, [&](const OwnedMappedBuffer<int, int>& existing) {
            destroyed = existing;
            ++destroy_calls; });

    INFO("successful resize reports replacement");
    REQUIRE(result == BufferResizeResult::Resized);
    INFO("successful resize destroys the previous buffer");
    REQUIRE(destroy_calls == 1);
    INFO("destroy callback receives the previous buffer");
    REQUIRE(destroyed.buffer == 11);
    INFO("successful resize installs the new buffer");
    REQUIRE(buffer.buffer == 33);
    INFO("successful resize installs the new allocation");
    REQUIRE(buffer.allocation == 44);
    INFO("successful resize updates the stored size");
    REQUIRE(buffer.size == static_cast<size_t>(128));
}

TEST_CASE("vulkan resource transaction cleans up a partial dependent allocation", "[renderer]")
{
    struct Resource
    {
        int primary = 0;
        int dependent = 0;
    };

    Resource output{ 7, 8 };
    int destroyed_primary = 0;
    const bool created = create_resource_transactionally(output,
        [](Resource& replacement) {
            replacement.primary = 42;
            return true;
        },
        [](Resource&) { return false; },
        [&](Resource& replacement) {
            destroyed_primary = replacement.primary;
            replacement = {};
        });

    REQUIRE_FALSE(created);
    REQUIRE(destroyed_primary == 42);
    REQUIRE(output.primary == 7);
    REQUIRE(output.dependent == 8);
}

TEST_CASE("full atlas uploads replace queued region uploads", "[renderer]")
{
    std::vector<PendingAtlasUpload> uploads;
    const uint8_t region[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t full[16] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };

    REQUIRE(queue_atlas_region_upload(uploads, 4, 5, 2, 1, region, 8, 8));
    REQUIRE(queue_full_atlas_upload(uploads, full, 2, 2, 8, 8));

    INFO("full upload discards earlier region uploads");
    REQUIRE(uploads.size() == static_cast<size_t>(1));
    INFO("queued upload is marked as full");
    REQUIRE(uploads.front().full_upload);
    INFO("full upload copies the full payload");
    REQUIRE(uploads.front().pixels.size() == static_cast<size_t>(16));
    INFO("full upload copies the payload bytes");
    REQUIRE(uploads.front().pixels.front() == static_cast<uint8_t>(10));
    INFO("full upload keeps the full payload");
    REQUIRE(uploads.front().pixels.back() == static_cast<uint8_t>(25));
    INFO("pending byte count tracks the queued full upload");
    REQUIRE(pending_atlas_upload_size_bytes(uploads) == static_cast<size_t>(16));
}

TEST_CASE("region atlas uploads patch a queued full upload", "[renderer]")
{
    std::vector<PendingAtlasUpload> uploads;
    const uint8_t full[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    const uint8_t region[4] = { 9, 8, 7, 6 };

    REQUIRE(queue_full_atlas_upload(uploads, full, 2, 2, 2, 2));
    REQUIRE(queue_atlas_region_upload(uploads, 1, 0, 1, 1, region, 2, 2));

    INFO("region uploads still keep a single queued full upload");
    REQUIRE(uploads.size() == static_cast<size_t>(1));
    INFO("full upload remains queued");
    REQUIRE(uploads.front().full_upload);
    INFO("patching a full upload preserves earlier pixels");
    REQUIRE(uploads.front().pixels[0] == static_cast<uint8_t>(1));
    INFO("region upload patches the queued full upload");
    REQUIRE(uploads.front().pixels[4] == static_cast<uint8_t>(9));
    INFO("patched payload keeps the new pixel data");
    REQUIRE(uploads.front().pixels[5] == static_cast<uint8_t>(8));
    INFO("patched payload keeps the full RGBA texel");
    REQUIRE(uploads.front().pixels[6] == static_cast<uint8_t>(7));
    INFO("patched payload keeps the full RGBA texel");
    REQUIRE(uploads.front().pixels[7] == static_cast<uint8_t>(6));
}

TEST_CASE("atlas upload queues reject invalid rectangles before reading payloads", "[renderer][atlas]")
{
    constexpr int max_int = std::numeric_limits<int>::max();
    const std::array<std::array<int, 4>, 10> invalid_rects = {{
        { -1, 0, 1, 1 }, { 0, -1, 1, 1 }, { 0, 0, -1, 1 }, { 0, 0, 1, -1 },
        { 0, 0, 0, 1 }, { 0, 0, 1, 0 }, { 1023, 0, 2, 1 }, { 0, 1023, 1, 2 },
        { max_int, 0, max_int, 1 }, { 0, max_int, 1, max_int }
    }};
    const uint8_t pixel[4] = { 1, 2, 3, 4 };

    for (const bool queued_full : { false, true })
    {
        std::vector<PendingAtlasUpload> uploads;
        if (queued_full)
            REQUIRE(queue_full_atlas_upload(uploads, pixel, 1, 1, 1024, 1024));
        for (const auto& rect : invalid_rects)
        {
            INFO("rectangle: " << rect[0] << ',' << rect[1] << ' ' << rect[2] << 'x' << rect[3]);
            REQUIRE_FALSE(queue_atlas_region_upload(uploads, rect[0], rect[1], rect[2], rect[3], pixel, 1024, 1024));
        }
        REQUIRE_FALSE(queue_atlas_region_upload(uploads, 0, 0, 1, 1, nullptr, 1024, 1024));
        REQUIRE_FALSE(queue_full_atlas_upload(uploads, pixel, 2048, 2048, 1024, 1024));
        REQUIRE_FALSE(queue_full_atlas_upload(uploads, nullptr, 1, 1, 1024, 1024));
        REQUIRE(uploads.size() == (queued_full ? 1u : 0u));
        if (queued_full)
            REQUIRE(uploads.front().pixels == std::vector<uint8_t>(pixel, pixel + 4));
    }
    REQUIRE(atlas_upload_size_bytes(max_int, max_int) == 0);
}

TEST_CASE("native atlas recording rejects corrupt and oversized queued copies", "[renderer][atlas]")
{
    std::vector<PendingAtlasUpload> uploads;
    const uint8_t pixel[4] = { 1, 2, 3, 4 };
    REQUIRE(queue_atlas_region_upload(uploads, 1023, 1023, 1, 1, pixel, 1024, 1024));
    size_t bytes = 0;
    REQUIRE(validate_pending_atlas_uploads(uploads, 1024, 1024, bytes));
    REQUIRE(bytes == 4);
    uploads.front().w = 2;
    REQUIRE_FALSE(validate_pending_atlas_uploads(uploads, 1024, 1024, bytes));
    uploads.front().w = 1;
    uploads.front().pixels.pop_back();
    REQUIRE_FALSE(validate_pending_atlas_uploads(uploads, 1024, 1024, bytes));
    uploads.front().pixels.push_back(4);
    uploads.front().x = std::numeric_limits<int>::max();
    REQUIRE_FALSE(validate_pending_atlas_uploads(uploads, 1024, 1024, bytes));
}

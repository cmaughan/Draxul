#include <draxul/bmp.h>
#include <draxul/perf_timing.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace draxul
{

namespace
{

constexpr int kBmpHeaderSize = 14;
constexpr int kBmpInfoSize = 40;
constexpr uint32_t kBmpMagic = 0x4D42;

void append_u16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void append_i32(std::vector<uint8_t>& out, int32_t value)
{
    append_u32(out, static_cast<uint32_t>(value));
}

} // namespace

bool write_bmp_rgba(const std::filesystem::path& path, const CapturedFrame& frame)
{
    PERF_MEASURE();
    if (!frame.valid())
        return false;

    const auto parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    const auto image_size = static_cast<uint32_t>(
        static_cast<uint64_t>(frame.width) * frame.height * 4);
    const auto file_size = kBmpHeaderSize + kBmpInfoSize + image_size;

    std::vector<uint8_t> bytes;
    bytes.reserve(file_size);

    append_u16(bytes, static_cast<uint16_t>(kBmpMagic));
    append_u32(bytes, file_size);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u32(bytes, kBmpHeaderSize + kBmpInfoSize);

    append_u32(bytes, kBmpInfoSize);
    append_i32(bytes, frame.width);
    append_i32(bytes, -frame.height);
    append_u16(bytes, 1);
    append_u16(bytes, 32);
    append_u32(bytes, 0);
    append_u32(bytes, image_size);
    append_i32(bytes, 0);
    append_i32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);

    for (size_t i = 0; i < frame.rgba.size(); i += 4)
    {
        bytes.push_back(frame.rgba[i + 2]);
        bytes.push_back(frame.rgba[i + 1]);
        bytes.push_back(frame.rgba[i + 0]);
        bytes.push_back(frame.rgba[i + 3]);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::optional<CapturedFrame> read_bmp_rgba(const std::filesystem::path& path)
{
    PERF_MEASURE();
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < kBmpHeaderSize + kBmpInfoSize)
        return std::nullopt;

    auto read_u16 = [&](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    };
    auto read_u32 = [&](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(bytes[offset])
            | (static_cast<uint32_t>(bytes[offset + 1]) << 8)
            | (static_cast<uint32_t>(bytes[offset + 2]) << 16)
            | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    auto read_i32 = [&](size_t offset) -> int32_t {
        return static_cast<int32_t>(read_u32(offset));
    };

    if (read_u16(0) != kBmpMagic)
        return std::nullopt;

    const uint32_t pixel_offset = read_u32(10);
    const int32_t width = read_i32(18);
    const int32_t height = read_i32(22);
    const uint16_t planes = read_u16(26);
    const uint16_t bpp = read_u16(28);
    const uint32_t compression = read_u32(30);

    if (planes != 1 || bpp != 32 || compression != 0 || width <= 0 || height == 0)
        return std::nullopt;

    // Guard against INT32_MIN: std::abs(INT32_MIN) is undefined behaviour.
    if (height == INT32_MIN)
        return std::nullopt;

    const bool top_down = height < 0;
    const int abs_height = std::abs(height);
    const size_t expected_size = static_cast<size_t>(width) * abs_height * 4;
    if (bytes.size() < pixel_offset + expected_size)
        return std::nullopt;

    CapturedFrame frame;
    frame.width = width;
    frame.height = abs_height;
    frame.rgba.resize(expected_size);

    for (int y = 0; y < abs_height; ++y)
    {
        const int src_y = top_down ? y : (abs_height - 1 - y);
        const auto src_row = pixel_offset + static_cast<size_t>(src_y) * static_cast<size_t>(width) * 4u;
        const auto dst_row = static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
        for (int x = 0; x < width; ++x)
        {
            const size_t src = src_row + static_cast<size_t>(x) * 4u;
            const size_t dst = dst_row + static_cast<size_t>(x) * 4u;
            frame.rgba[dst + 0] = bytes[src + 2];
            frame.rgba[dst + 1] = bytes[src + 1];
            frame.rgba[dst + 2] = bytes[src + 0];
            frame.rgba[dst + 3] = bytes[src + 3];
        }
    }

    return frame;
}

} // namespace draxul

#include "satview_star_catalog.h"

#include "satview_texture_assets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <fstream>

namespace draxul::satview
{

namespace
{

constexpr std::array<char, 8> kStarCatalogMagic = { 'D', 'X', 'S', 'T', 'A', 'R', '1', '\0' };
constexpr std::uint32_t kStarCatalogVersion = 1;

struct StarCatalogHeader
{
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
    std::uint32_t record_size = 0;
    std::uint32_t record_count = 0;
    std::uint32_t flags = 0;
    std::uint32_t source_id = 0;
};
static_assert(sizeof(StarCatalogHeader) == 32);

struct StarCatalogRecord
{
    float direction_magnitude[4]{};
    float color_size[4]{};
};
static_assert(sizeof(StarCatalogRecord) == 32);

} // namespace

std::vector<SatViewStarInstance> load_satview_star_catalog(std::size_t maximum_count)
{
    PERF_MEASURE();
    const auto path = resolve_satview_asset_path("catalog/stars.dxstar");
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: star catalog not available at %s",
            path.string().c_str());
        return {};
    }

    StarCatalogHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kStarCatalogMagic
        || header.version != kStarCatalogVersion
        || header.header_size < sizeof(StarCatalogHeader)
        || header.record_size != sizeof(StarCatalogRecord))
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: unsupported star catalog format at %s",
            path.string().c_str());
        return {};
    }

    file.seekg(static_cast<std::streamoff>(header.header_size), std::ios::beg);
    const std::size_t count = std::min<std::size_t>(header.record_count, maximum_count);
    std::vector<SatViewStarInstance> stars;
    stars.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        StarCatalogRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(record));
        if (!file)
            break;

        SatViewStarInstance star;
        star.direction_magnitude = glm::vec4(
            record.direction_magnitude[0],
            record.direction_magnitude[1],
            record.direction_magnitude[2],
            record.direction_magnitude[3]);
        star.color_size = glm::vec4(
            record.color_size[0],
            record.color_size[1],
            record.color_size[2],
            record.color_size[3]);
        stars.push_back(star);
    }

    DRAXUL_LOG_INFO(LogCategory::Renderer,
        "SatView: loaded %zu stars from %s",
        stars.size(),
        path.string().c_str());
    return stars;
}

} // namespace draxul::satview

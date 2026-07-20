#include "satview_star_catalog.h"

#include <draxul/satview/satview_catalog_container.h>
#include <draxul/satview/satview_texture_assets.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <fstream>
#include <optional>

namespace draxul::satview
{

namespace
{

constexpr std::array<char, 8> kStarCatalogMagic = { 'D', 'X', 'S', 'T', 'A', 'R', '1', '\0' };
constexpr std::uint32_t kStarCatalogVersion = 1;

struct StarCatalogRecord
{
    float direction_magnitude[4]{};
    float color_size[4]{};
};
static_assert(sizeof(StarCatalogRecord) == 32);

} // namespace

std::vector<SatViewStarInstance> load_satview_star_catalog(std::size_t maximum_count)
{
    return load_satview_star_catalog(maximum_count, resolve_satview_asset_path("catalog/stars.dxstar"));
}

std::vector<SatViewStarInstance> load_satview_star_catalog(
    std::size_t maximum_count, const std::filesystem::path& path)
{
    PERF_MEASURE();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: star catalog not available at %s",
            path.string().c_str());
        return {};
    }

    const std::optional<SingleTableCatalogHeader> header = open_single_table_catalog(
        file, kStarCatalogMagic, kStarCatalogVersion,
        static_cast<std::uint32_t>(sizeof(StarCatalogRecord)));
    if (!header)
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: unsupported star catalog format at %s",
            path.string().c_str());
        return {};
    }

    const std::size_t count = std::min<std::size_t>(header->record_count, maximum_count);
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

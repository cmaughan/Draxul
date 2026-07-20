#include "satview_constellation_catalog.h"

#include <draxul/satview/satview_catalog_container.h>
#include <draxul/satview/satview_texture_assets.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <fstream>
#include <glm/geometric.hpp>
#include <optional>

namespace draxul::satview
{

namespace
{

constexpr std::array<char, 8> kConstellationCatalogMagic = {
    'D', 'X', 'C', 'L', 'I', 'N', 'E', '1'
};
constexpr std::uint32_t kConstellationCatalogVersion = 1;
constexpr glm::vec4 kConstellationLineColor = { 0.46f, 0.62f, 0.82f, 0.24f };

struct ConstellationCatalogRecord
{
    float start[3]{};
    float end[3]{};
};
static_assert(sizeof(ConstellationCatalogRecord) == 24);

bool valid_direction(const glm::vec3& direction)
{
    return std::isfinite(direction.x)
        && std::isfinite(direction.y)
        && std::isfinite(direction.z)
        && glm::dot(direction, direction) > 0.000001f;
}

} // namespace

std::vector<SatViewCelestialLineInstance> load_satview_constellation_catalog()
{
    return load_satview_constellation_catalog(
        resolve_satview_asset_path("catalog/constellations.dxline"));
}

std::vector<SatViewCelestialLineInstance> load_satview_constellation_catalog(
    const std::filesystem::path& path)
{
    PERF_MEASURE();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: constellation catalog not available at %s",
            path.string().c_str());
        return {};
    }

    const std::optional<SingleTableCatalogHeader> header = open_single_table_catalog(
        file, kConstellationCatalogMagic, kConstellationCatalogVersion,
        static_cast<std::uint32_t>(sizeof(ConstellationCatalogRecord)));
    if (!header)
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: unsupported constellation catalog format at %s",
            path.string().c_str());
        return {};
    }

    std::vector<SatViewCelestialLineInstance> lines;
    lines.reserve(header->record_count);
    for (std::uint32_t index = 0; index < header->record_count; ++index)
    {
        ConstellationCatalogRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(record));
        if (!file)
            break;

        const glm::vec3 start(record.start[0], record.start[1], record.start[2]);
        const glm::vec3 end(record.end[0], record.end[1], record.end[2]);
        if (!valid_direction(start) || !valid_direction(end))
            continue;
        const glm::vec3 start_direction = glm::normalize(start);
        const glm::vec3 end_direction = glm::normalize(end);
        lines.push_back({
            glm::vec4(start_direction, 1.35f),
            glm::vec4(end_direction, 0.0f),
            kConstellationLineColor,
            glm::vec4(0.0f),
        });
    }

    DRAXUL_LOG_INFO(LogCategory::Renderer,
        "SatView: loaded %zu constellation segments from %s",
        lines.size(),
        path.string().c_str());
    return lines;
}

} // namespace draxul::satview

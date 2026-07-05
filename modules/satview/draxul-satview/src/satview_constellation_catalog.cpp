#include "satview_constellation_catalog.h"

#include "satview_texture_assets.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <fstream>
#include <glm/geometric.hpp>

namespace draxul::satview
{

namespace
{

constexpr std::array<char, 8> kConstellationCatalogMagic = {
    'D', 'X', 'C', 'L', 'I', 'N', 'E', '1'
};
constexpr std::uint32_t kConstellationCatalogVersion = 1;
constexpr glm::vec4 kConstellationLineColor = { 0.46f, 0.62f, 0.82f, 0.24f };

struct ConstellationCatalogHeader
{
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
    std::uint32_t record_size = 0;
    std::uint32_t record_count = 0;
    std::uint32_t flags = 0;
    std::uint32_t source_id = 0;
};
static_assert(sizeof(ConstellationCatalogHeader) == 32);

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

std::vector<SatViewSceneVertex> load_satview_constellation_catalog()
{
    PERF_MEASURE();
    const auto path = resolve_satview_asset_path("catalog/constellations.dxline");
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: constellation catalog not available at %s",
            path.string().c_str());
        return {};
    }

    ConstellationCatalogHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kConstellationCatalogMagic
        || header.version != kConstellationCatalogVersion
        || header.header_size < sizeof(ConstellationCatalogHeader)
        || header.record_size != sizeof(ConstellationCatalogRecord))
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: unsupported constellation catalog format at %s",
            path.string().c_str());
        return {};
    }

    file.seekg(static_cast<std::streamoff>(header.header_size), std::ios::beg);
    std::vector<SatViewSceneVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(header.record_count) * 2u);
    for (std::uint32_t index = 0; index < header.record_count; ++index)
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
        vertices.push_back({
            glm::vec4(start_direction, -1.0f),
            kConstellationLineColor,
            glm::vec4(end_direction, 1.0f),
        });
        vertices.push_back({
            glm::vec4(end_direction, 1.0f),
            kConstellationLineColor,
            glm::vec4(start_direction, -1.0f),
        });
    }

    DRAXUL_LOG_INFO(LogCategory::Renderer,
        "SatView: loaded %zu constellation segments from %s",
        vertices.size() / 2u,
        path.string().c_str());
    return vertices;
}

} // namespace draxul::satview

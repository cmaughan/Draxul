#include <draxul/satview/satview_constellation_boundary_catalog.h>

#include <draxul/satview/satview_catalog_container.h>
#include <draxul/satview/satview_texture_assets.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <fstream>
#include <glm/geometric.hpp>

namespace draxul::satview
{

namespace
{

constexpr std::array<char, 8> kMagic = { 'D', 'X', 'C', 'B', 'N', 'D', '0', '1' };
constexpr std::uint32_t kVersion = 1;

struct CatalogHeader
{
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
    std::uint32_t segment_record_size = 0;
    std::uint32_t label_record_size = 0;
    std::uint32_t segment_count = 0;
    std::uint32_t label_count = 0;
    std::uint32_t segment_offset = 0;
    std::uint32_t label_offset = 0;
    std::uint32_t string_offset = 0;
    std::uint32_t string_size = 0;
    std::uint32_t source_id = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(CatalogHeader) == 56);

struct SegmentRecord
{
    float start[3]{};
    float end[3]{};
    std::uint32_t source_feature = 0;
    std::uint32_t flags = 0;
};
static_assert(sizeof(SegmentRecord) == 32);

struct LabelRecord
{
    float direction[3]{};
    std::uint32_t rank = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t name_size = 0;
    std::array<char, 4> designation{};
    std::uint32_t area_index = 0;
};
static_assert(sizeof(LabelRecord) == 32);

bool valid_direction(const glm::vec3& direction)
{
    return std::isfinite(direction.x)
        && std::isfinite(direction.y)
        && std::isfinite(direction.z)
        && glm::dot(direction, direction) > 0.000001f;
}

std::string designation_string(const std::array<char, 4>& value)
{
    const auto end = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), end);
}

} // namespace

SatViewConstellationBoundaryCatalog load_satview_constellation_boundary_catalog(
    const std::filesystem::path& path)
{
    PERF_MEASURE();
    SatViewConstellationBoundaryCatalog catalog;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: constellation boundary catalog not available at %s",
            path.string().c_str());
        return catalog;
    }

    const std::streamoff stream_size = file.tellg();
    if (stream_size < 0)
        return catalog;
    const std::uint64_t file_size = static_cast<std::uint64_t>(stream_size);
    file.seekg(0, std::ios::beg);

    CatalogHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    const std::uint64_t segment_bytes = static_cast<std::uint64_t>(header.segment_count) * sizeof(SegmentRecord);
    const std::uint64_t label_bytes = static_cast<std::uint64_t>(header.label_count) * sizeof(LabelRecord);
    if (!file || header.magic != kMagic || header.version != kVersion
        || header.header_size != sizeof(CatalogHeader)
        || header.segment_record_size != sizeof(SegmentRecord)
        || header.label_record_size != sizeof(LabelRecord)
        || header.segment_count > kCatalogContainerMaximumRecordCount
        || header.label_count > kCatalogContainerMaximumRecordCount
        || header.string_size > kCatalogContainerMaximumStringBytes
        || !catalog_container_range_fits(header.segment_offset, segment_bytes, file_size)
        || !catalog_container_range_fits(header.label_offset, label_bytes, file_size)
        || !catalog_container_range_fits(header.string_offset, header.string_size, file_size))
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer,
            "SatView: unsupported constellation boundary catalog at %s",
            path.string().c_str());
        return catalog;
    }

    file.seekg(static_cast<std::streamoff>(header.string_offset), std::ios::beg);
    std::string strings(header.string_size, '\0');
    file.read(strings.data(), static_cast<std::streamsize>(strings.size()));
    if (!file)
        return {};

    file.seekg(static_cast<std::streamoff>(header.segment_offset), std::ios::beg);
    catalog.segments.reserve(header.segment_count);
    for (std::uint32_t index = 0; index < header.segment_count; ++index)
    {
        SegmentRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(record));
        if (!file)
            return {};
        glm::vec3 start(record.start[0], record.start[1], record.start[2]);
        glm::vec3 end(record.end[0], record.end[1], record.end[2]);
        if (!valid_direction(start) || !valid_direction(end))
            return {};
        catalog.segments.push_back({
            glm::normalize(start),
            glm::normalize(end),
            record.source_feature,
        });
    }

    file.seekg(static_cast<std::streamoff>(header.label_offset), std::ios::beg);
    catalog.labels.reserve(header.label_count);
    for (std::uint32_t index = 0; index < header.label_count; ++index)
    {
        LabelRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(record));
        if (!file || record.rank < 1 || record.rank > 3
            || !catalog_container_range_fits(record.name_offset, record.name_size, strings.size()))
        {
            return {};
        }
        glm::vec3 direction(
            record.direction[0], record.direction[1], record.direction[2]);
        const std::string designation = designation_string(record.designation);
        if (!valid_direction(direction) || designation.size() != 3)
            return {};
        catalog.labels.push_back({
            glm::normalize(direction),
            record.rank,
            designation,
            strings.substr(record.name_offset, record.name_size),
            record.area_index,
        });
    }

    DRAXUL_LOG_INFO(LogCategory::Renderer,
        "SatView: loaded %zu constellation boundary segments and %zu area labels from %s",
        catalog.segments.size(),
        catalog.labels.size(),
        path.string().c_str());
    return catalog;
}

SatViewConstellationBoundaryCatalog load_satview_constellation_boundary_catalog()
{
    return load_satview_constellation_boundary_catalog(
        resolve_satview_asset_path("catalog/constellation_boundaries.dxbnd"));
}

} // namespace draxul::satview

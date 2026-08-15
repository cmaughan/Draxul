#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace draxul::satview
{

struct SatViewConstellationBoundarySegment
{
    glm::vec3 start_direction{ 0.0f };
    glm::vec3 end_direction{ 0.0f };
    std::uint32_t source_feature = 0;
};

struct SatViewConstellationAreaLabel
{
    glm::vec3 direction{ 0.0f };
    std::uint32_t rank = 3;
    std::string designation;
    std::string name;
    std::uint32_t area_index = 0;
};

struct SatViewConstellationBoundaryCatalog
{
    std::vector<SatViewConstellationBoundarySegment> segments;
    std::vector<SatViewConstellationAreaLabel> labels;
};

[[nodiscard]] SatViewConstellationBoundaryCatalog load_satview_constellation_boundary_catalog();
[[nodiscard]] SatViewConstellationBoundaryCatalog load_satview_constellation_boundary_catalog(
    const std::filesystem::path& path);

} // namespace draxul::satview

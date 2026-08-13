#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/geometry_mesh.h>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{

struct StaticBuildingLayerSpec
{
    float height_fraction = 1.0f;
    float color_multiplier = 1.0f;
    uint32_t layer_id = 0;
};

struct StaticBuildingRingSpec
{
    int sides = 4;
    float middle_strip_scale = 1.0f;
    float level_gap_fraction = 0.0f;
    std::vector<StaticBuildingLayerSpec> layers;
};

struct StaticBrickStackSpec
{
    int grid_size = 2;
    float brick_gap_fraction = 0.0f;
    float floor_gap_fraction = 0.0f;
    std::vector<StaticBuildingLayerSpec> bricks;
};

struct StaticRoofSignRingSpec
{
    int sides = 4;
    float inner_radius_fraction = 0.45f;
};

struct StaticSidewalkRingSpec
{
    int sides = 4;
    float inner_radius_fraction = 0.45f;
    glm::vec3 color{ 1.0f };
};

class StaticMeshFamilyCache
{
public:
    [[nodiscard]] std::shared_ptr<const GeometryMesh> building_ring(const StaticBuildingRingSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> brick_stack(const StaticBrickStackSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> building_cap(int sides, float middle_strip_scale, uint32_t layer_id);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> roof_sign_ring(const StaticRoofSignRingSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> sidewalk_ring(const StaticSidewalkRingSpec& spec);

    [[nodiscard]] size_t size() const;
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<const GeometryMesh>> building_rings_;
    std::unordered_map<std::string, std::shared_ptr<const GeometryMesh>> brick_stacks_;
    std::unordered_map<std::string, std::shared_ptr<const GeometryMesh>> building_caps_;
    std::unordered_map<std::string, std::shared_ptr<const GeometryMesh>> roof_sign_rings_;
    std::unordered_map<std::string, std::shared_ptr<const GeometryMesh>> sidewalk_rings_;
};

} // namespace draxul

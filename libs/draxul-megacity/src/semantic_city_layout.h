#pragma once

#include "scene_components.h"

#include <draxul/citydb.h>

#include <array>
#include <glm/vec2.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

// Fraction of road width reserved beyond the sidewalk on each side of a building lot.
constexpr float kLotRoadReserveFraction = 0.25f;

struct SemanticBuildingLayer
{
    int function_size = 0;
    float height = 1.0f;
};

struct SemanticCityBuilding
{
    std::string module_path;
    std::string display_name;
    std::string qualified_name;
    std::string source_file_path;
    BuildingMetrics metrics;
    glm::vec2 center{ 0.0f };
    std::vector<SemanticBuildingLayer> layers;
};

struct SemanticCityModuleInput
{
    std::string module_path;
    std::vector<CityClassRecord> rows;
};

struct SemanticCityModuleLayout
{
    std::string module_path;
    std::vector<SemanticCityBuilding> buildings;
    glm::vec2 offset{ 0.0f };
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;

    [[nodiscard]] bool empty() const
    {
        return buildings.empty();
    }
};

struct RoadSegmentPlacement
{
    glm::vec2 center{ 0.0f };
    glm::vec2 extent{ 1.0f };
};

struct SemanticCityLayout
{
    std::vector<SemanticCityBuilding> buildings;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;

    [[nodiscard]] bool empty() const
    {
        return buildings.empty();
    }
};

struct SemanticMegacityLayout
{
    std::vector<SemanticCityModuleLayout> modules;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;

    [[nodiscard]] bool empty() const
    {
        return modules.empty();
    }

    [[nodiscard]] size_t building_count() const
    {
        size_t count = 0;
        for (const auto& module : modules)
            count += module.buildings.size();
        return count;
    }
};

[[nodiscard]] BuildingMetrics derive_building_metrics(const CityClassRecord& row, bool clamp_metrics = true, float height_multiplier = 1.5f);
[[nodiscard]] bool is_test_semantic_source(std::string_view source_file_path);
[[nodiscard]] std::array<RoadSegmentPlacement, 4> build_sidewalk_segments(
    const SemanticCityBuilding& building);
[[nodiscard]] std::array<RoadSegmentPlacement, 4> build_road_segments(
    const SemanticCityBuilding& building);
[[nodiscard]] SemanticCityLayout build_semantic_city_layout(
    const std::vector<CityClassRecord>& rows, bool clamp_metrics = true, bool hide_test_entities = false, float height_multiplier = 1.5f);
[[nodiscard]] SemanticMegacityLayout build_semantic_megacity_layout(
    const std::vector<SemanticCityModuleInput>& modules, bool clamp_metrics = true, bool hide_test_entities = false, float height_multiplier = 1.5f);

} // namespace draxul

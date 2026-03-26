#pragma once

#include "scene_components.h"

#include <draxul/citydb.h>
#include <draxul/megacity_code_config.h>

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

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
    int base_size = 0;
    int function_count = 0;
    int function_mass = 0;
    int road_size = 0;
    BuildingMetrics metrics;
    glm::vec2 center{ 0.0f };
    std::vector<SemanticBuildingLayer> layers;
};

struct SemanticCityModuleModel
{
    std::string module_path;
    int connectivity = 0;
    std::vector<SemanticCityBuilding> buildings;

    [[nodiscard]] bool empty() const
    {
        return buildings.empty();
    }
};

struct SemanticMegacityModel
{
    std::vector<SemanticCityModuleModel> modules;

    [[nodiscard]] bool empty() const
    {
        return modules.empty();
    }

    [[nodiscard]] size_t building_count() const
    {
        size_t count = 0;
        for (const auto& module_model : modules)
            count += module_model.buildings.size();
        return count;
    }
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
        for (const auto& module_layout : modules)
            count += module_layout.buildings.size();
        return count;
    }
};

[[nodiscard]] BuildingMetrics derive_building_metrics(
    const CityClassRecord& row, const MegaCityCodeConfig& config);
[[nodiscard]] bool is_test_semantic_source(std::string_view source_file_path);
[[nodiscard]] SemanticCityModuleModel build_semantic_city_model(
    std::string_view module_path, const std::vector<CityClassRecord>& rows, const MegaCityCodeConfig& config);
[[nodiscard]] SemanticMegacityModel build_semantic_megacity_model(
    const std::vector<SemanticCityModuleInput>& modules, const MegaCityCodeConfig& config);
[[nodiscard]] std::array<RoadSegmentPlacement, 4> build_sidewalk_segments(
    const SemanticCityBuilding& building);
[[nodiscard]] std::array<RoadSegmentPlacement, 4> build_road_segments(
    const SemanticCityBuilding& building);
[[nodiscard]] SemanticCityLayout build_semantic_city_layout(
    const SemanticCityModuleModel& module_model, const MegaCityCodeConfig& config);
[[nodiscard]] SemanticCityLayout build_semantic_city_layout(
    const std::vector<CityClassRecord>& rows, const MegaCityCodeConfig& config);
[[nodiscard]] SemanticMegacityLayout build_semantic_megacity_layout(
    const SemanticMegacityModel& model, const MegaCityCodeConfig& config);
[[nodiscard]] SemanticMegacityLayout build_semantic_megacity_layout(
    const std::vector<SemanticCityModuleInput>& modules, const MegaCityCodeConfig& config);

// 2D occupancy grid for city overview and pathfinding.
// Lives in the presentation model so both the ImGui panel and 3D scene can use it.
struct CityGrid
{
    std::vector<uint8_t> cells; // row-major: cells[row * cols + col]
    int cols = 0;
    int rows = 0;
    float cell_size = 0.5f; // world units per cell (matches placement_step)
    float origin_x = 0.0f; // world X of cell (0,0)
    float origin_z = 0.0f; // world Z of cell (0,0)

    [[nodiscard]] uint8_t at(int col, int row) const
    {
        if (col < 0 || col >= cols || row < 0 || row >= rows)
            return 0;
        return cells[static_cast<size_t>(row) * cols + col];
    }
};

// Cell values for CityGrid
inline constexpr uint8_t kCityGridEmpty = 0;
inline constexpr uint8_t kCityGridBuilding = 1;
inline constexpr uint8_t kCityGridSidewalk = 2;
inline constexpr uint8_t kCityGridRoad = 3;

[[nodiscard]] CityGrid build_city_grid(
    const SemanticMegacityLayout& layout, const MegaCityCodeConfig& config);

} // namespace draxul

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
    std::string function_name;
    int function_size = 0;
    float height = 1.0f;
};

struct SemanticCityBuilding
{
    std::string module_path;
    std::string display_name;
    std::string qualified_name;
    std::string source_file_path;
    bool is_struct = false;
    int base_size = 0;
    int function_count = 0;
    int function_mass = 0;
    int road_size = 0;
    BuildingMetrics metrics;
    glm::vec2 center{ 0.0f };
    std::vector<SemanticBuildingLayer> layers;
};

struct SemanticCityDependency
{
    std::string source_module_path;
    std::string source_qualified_name;
    std::string field_name;
    std::string field_type_name;
    std::string target_module_path;
    std::string target_qualified_name;
    std::string source_file_path;
    std::string target_file_path;
    bool is_abstract_ref = false;
};

struct SemanticCityModuleModel
{
    std::string module_path;
    int connectivity = 0;
    float quality = 0.5f;
    CodebaseHealthMetrics health;
    std::vector<SemanticCityBuilding> buildings;

    [[nodiscard]] bool empty() const
    {
        return buildings.empty();
    }
};

struct SemanticMegacityModel
{
    std::vector<SemanticCityModuleModel> modules;
    std::vector<SemanticCityDependency> dependencies;
    CodebaseHealthMetrics codebase_health; // global metrics across all modules

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
    std::vector<CityDependencyRecord> dependencies;
    float quality = 0.5f; // 0..1, from CityModuleRecord
    CodebaseHealthMetrics health;
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
    float quality = 0.5f;
    CodebaseHealthMetrics health;
    glm::vec2 park_center{ 0.0f };
    float park_footprint = 0.0f; // 0 means no park
    float park_sidewalk_width = 0.0f;
    float park_road_width = 0.0f;
    bool is_central_park = false;

    [[nodiscard]] bool empty() const
    {
        return buildings.empty() && !is_central_park;
    }
};

struct RoadSegmentPlacement
{
    glm::vec2 center{ 0.0f };
    glm::vec2 extent{ 1.0f };
};

struct ModuleBoundarySignPlacement
{
    glm::vec2 center{ 0.0f };
    float width = 1.0f;
    float depth = 0.025f;
    float yaw_radians = 0.0f;
};

struct CitySurfaceBounds
{
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;

    [[nodiscard]] bool valid() const
    {
        return max_x > min_x && max_z > min_z;
    }
};

struct SemanticCityLayout
{
    std::vector<SemanticCityBuilding> buildings;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    glm::vec2 park_center{ 0.0f };
    float park_footprint = 0.0f;
    float park_sidewalk_width = 0.0f;
    float park_road_width = 0.0f;

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
[[nodiscard]] float compute_module_border_width(
    const SemanticCityModuleLayout& module_layout, const MegaCityCodeConfig& config);
[[nodiscard]] std::array<ModuleBoundarySignPlacement, 2> build_module_boundary_sign_placements(
    const SemanticCityModuleLayout& module_layout, const MegaCityCodeConfig& config);
[[nodiscard]] CitySurfaceBounds compute_city_road_surface_bounds(
    const SemanticMegacityLayout& layout);
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
    struct RoutePolyline
    {
        std::string source_file_path;
        std::string source_module_path;
        std::string source_qualified_name;
        std::string target_file_path;
        std::string target_module_path;
        std::string target_qualified_name;
        std::string field_name;
        std::string field_type_name;
        glm::vec4 source_color{ 1.0f };
        glm::vec4 target_color{ 1.0f };
        std::vector<glm::vec2> world_points;
        float elevation = 0.0f;
    };

    struct RouteRenderSegment
    {
        glm::vec2 a{ 0.0f };
        glm::vec2 b{ 0.0f };
        glm::vec4 color{ 1.0f };
        std::string source_module_path;
        std::string source_qualified_name;
        std::string target_module_path;
        std::string target_qualified_name;
    };

    std::vector<uint8_t> cells; // row-major: cells[row * cols + col]
    std::vector<RoutePolyline> routes;
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
inline constexpr uint8_t kCityGridPark = 4;

[[nodiscard]] CityGrid build_city_grid(
    const SemanticMegacityLayout& layout, const MegaCityCodeConfig& config);
[[nodiscard]] CityGrid build_city_grid(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const MegaCityCodeConfig& config);
[[nodiscard]] std::vector<CityGrid::RoutePolyline> build_city_routes(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const MegaCityCodeConfig& config);
[[nodiscard]] std::vector<CityGrid::RoutePolyline> build_city_routes_for_selection(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const CityGrid& grid,
    const MegaCityCodeConfig& config,
    std::string_view focus_source_file_path,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name);
[[nodiscard]] std::vector<CityGrid::RouteRenderSegment> build_city_route_render_segments(
    const std::vector<CityGrid::RoutePolyline>& routes, float lane_spacing);

} // namespace draxul

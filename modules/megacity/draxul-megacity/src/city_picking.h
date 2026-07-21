#pragma once

#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <string>

namespace draxul
{

class IsometricCamera;
struct MegaCityCodeConfig;
struct SemanticMegacityModel;
struct SemanticMegacityLayout;
struct CityGrid;

struct PickResult
{
    std::string qualified_name;
    std::string module_path;
    std::string source_file_path;
    glm::vec2 building_center{ 0.0f };
    float hit_y = 0.0f;
    uint32_t layer_index = 0u;
    bool has_layer_index = false;
};

struct CityFeaturePick
{
    enum class Kind
    {
        Park,
        Route,
    };

    Kind kind = Kind::Park;
    std::string module_path;
    float park_quality = 0.0f;
    float park_footprint = 0.0f;
    bool is_central_park = false;
    std::string route_source;
    std::string route_target;
    std::string route_field_name;
    std::string route_field_type;
};

// Pick a building by casting a ray from screen coordinates through the isometric camera.
// Returns the closest building hit, or std::nullopt if nothing was hit.
std::optional<PickResult> pick_building(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityLayout& layout,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& filter = {},
    const SemanticMegacityModel* model = nullptr,
    const MegaCityCodeConfig* config = nullptr);

// Pick non-building city presentation features. Route picking is opt-in so the
// normal delayed hover path does not make dependency routes intrusive.
std::optional<CityFeaturePick> pick_city_feature(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityLayout& layout,
    const MegaCityCodeConfig& config,
    const CityGrid* grid = nullptr,
    bool include_routes = false);

} // namespace draxul

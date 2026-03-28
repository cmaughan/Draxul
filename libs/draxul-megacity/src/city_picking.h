#pragma once

#include <glm/vec2.hpp>
#include <optional>
#include <string>

namespace draxul
{

class IsometricCamera;
struct SemanticMegacityModel;

struct PickResult
{
    std::string qualified_name;
    std::string module_path;
    glm::vec2 building_center{ 0.0f };
    float hit_y = 0.0f;
};

// Pick a building by casting a ray from screen coordinates through the isometric camera.
// Returns the closest building hit, or std::nullopt if nothing was hit.
std::optional<PickResult> pick_building(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityModel& model);

} // namespace draxul

#pragma once

#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <string>

namespace draxul
{

class IsometricCamera;
struct SemanticMegacityLayout;

struct PickResult
{
    std::string qualified_name;
    std::string module_path;
    std::string source_file_path;
    glm::vec2 building_center{ 0.0f };
    float hit_y = 0.0f;
};

// Pick a building by casting a ray from screen coordinates through the isometric camera.
// Returns the closest building hit, or std::nullopt if nothing was hit.
std::optional<PickResult> pick_building(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityLayout& layout,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& filter = {});

} // namespace draxul

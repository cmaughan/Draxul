#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{

class TextService;

struct BuildingTooltipData
{
    std::string name;
    std::string module_path;
    int function_count = 0;
    int field_count = 0;
    std::string hovered_function;

    // Route tooltip (when hovering a dependency connection).
    std::string route_source;
    std::string route_target;
    std::string route_field_name;
    std::string route_field_type;

    // Park tooltip.
    std::string park_module;
    float park_quality = 0.0f;
    float park_footprint = 0.0f;

    // Tree tooltip.
    float tree_height = 0.0f;
    float tree_canopy_radius = 0.0f;

    [[nodiscard]] bool is_route() const
    {
        return !route_source.empty();
    }
    [[nodiscard]] bool is_park() const
    {
        return !park_module.empty();
    }
    [[nodiscard]] bool is_tree() const
    {
        return tree_height > 0.0f;
    }
};

struct TooltipBitmap
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

/// Rasterize a multi-line tooltip bitmap with a semi-transparent dark background
/// and light text showing the building's metadata.
[[nodiscard]] TooltipBitmap rasterize_tooltip(
    TextService& text_service, const BuildingTooltipData& data);

} // namespace draxul

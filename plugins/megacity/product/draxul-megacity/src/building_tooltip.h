#pragma once

#include <cstdint>
#include <draxul/gui/tooltip.h>
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
    bool is_function_bundle = false;
    bool is_struct_stack = false;
    std::string hovered_function;
    int hovered_field_count = 0;
    bool has_building_perf = false;
    float building_frame_fraction = 0.0f;
    float building_smoothed_frame_fraction = 0.0f;
    float building_heat = 0.0f;
    bool has_function_perf = false;
    float function_frame_fraction = 0.0f;
    float function_smoothed_frame_fraction = 0.0f;
    float function_heat = 0.0f;

    // LCOV coverage mode indicator
    bool lcov_mode = false;

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

    /// Convert to generic GUI tooltip data.
    [[nodiscard]] draxul::gui::TooltipData to_gui_data() const;
};

// Re-alias TooltipBitmap for megacity's internal use to avoid breaking too much code.
using TooltipBitmap = draxul::gui::TooltipBitmap;

/// Rasterize a multi-line tooltip bitmap with a semi-transparent dark background
/// and light text showing the building's metadata.
[[nodiscard]] TooltipBitmap rasterize_building_tooltip(
    TextService& text_service, const BuildingTooltipData& data);

} // namespace draxul

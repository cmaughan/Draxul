#include "building_tooltip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>

namespace draxul
{

draxul::gui::TooltipData BuildingTooltipData::to_gui_data() const
{
    PERF_MEASURE();
    draxul::gui::TooltipData gui_data;

    auto fmt_float = [](float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
        return std::string(buf);
    };
    auto fmt_percent = [](float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v * 100.0f));
        return std::string(buf);
    };
    auto fmt_heat = [](float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
        return std::string(buf);
    };

    if (is_route())
    {
        gui_data.entries = {
            { "Field", route_field_name },
            { "Type", route_field_type },
            { "From", route_source },
            { "To", route_target },
        };
    }
    else if (is_tree())
    {
        gui_data.entries = {
            { "Name", name },
            { "Module", module_path },
            { "Height", fmt_float(tree_height) },
            { "Canopy", fmt_float(tree_canopy_radius) },
        };
    }
    else if (is_park())
    {
        gui_data.entries = {
            { "Name", name },
            { "Module", park_module },
            { "Quality", fmt_float(park_quality * 100.0f) + "%" },
            { "Size", fmt_float(park_footprint) },
        };
    }
    else
    {
        if (is_struct_stack)
        {
            gui_data.entries = {
                { "File", module_path },
                { "Structs", std::to_string(function_count) },
            };
            if (!hovered_function.empty())
            {
                gui_data.entries.push_back({ "Struct", hovered_function });
                gui_data.entries.push_back({ "Fields", std::to_string(hovered_field_count) });
            }
        }
        else
        {
            gui_data.entries = {
                { "Name", name },
                { is_function_bundle ? "File" : "Module", module_path },
                { "Functions", std::to_string(function_count) },
            };
            if (!is_function_bundle)
                gui_data.entries.push_back({ "Fields", std::to_string(field_count) });
            if (!hovered_function.empty())
                gui_data.entries.push_back({ "Function", hovered_function });
        }

        if (lcov_mode)
        {
            if (has_building_perf)
                gui_data.entries.push_back({ "Coverage", building_heat > 0.0f ? "Covered" : "Not covered" });
            if (!hovered_function.empty() && has_function_perf)
                gui_data.entries.push_back({ "Fn Coverage", function_heat > 0.0f ? "Covered" : "Not covered" });
        }
        else
        {
            if (has_building_perf)
            {
                gui_data.entries.push_back({ "Frame", fmt_percent(building_frame_fraction) });
                gui_data.entries.push_back({ "Avg", fmt_percent(building_smoothed_frame_fraction) });
                gui_data.entries.push_back({ "Heat", fmt_heat(building_heat) });
            }
            if (!hovered_function.empty() && has_function_perf)
            {
                gui_data.entries.push_back({ "Fn Frame", fmt_percent(function_frame_fraction) });
                gui_data.entries.push_back({ "Fn Avg", fmt_percent(function_smoothed_frame_fraction) });
                gui_data.entries.push_back({ "Fn Heat", fmt_heat(function_heat) });
            }
        }
    }

    return gui_data;
}

TooltipBitmap rasterize_building_tooltip(TextService& text_service, const BuildingTooltipData& data)
{
    return draxul::gui::rasterize_tooltip(text_service, data.to_gui_data());
}

} // namespace draxul

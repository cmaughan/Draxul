#include "ui_city_map_panel.h"

#include "semantic_city_layout.h"

#include <draxul/perf_timing.h>
#include <imgui.h>

namespace draxul
{

void render_city_map_panel(const std::shared_ptr<const CityGrid>& grid, bool building_in_progress)
{
    PERF_MEASURE();
    if (!ImGui::Begin("City Map"))
    {
        ImGui::End();
        return;
    }

    if (building_in_progress)
    {
        ImGui::TextUnformatted("Building grid...");
        ImGui::End();
        return;
    }

    if (!grid || grid->cells.empty())
    {
        ImGui::TextUnformatted("No city grid available.");
        ImGui::End();
        return;
    }

    ImGui::Text("%dx%d cells (%.2f unit/cell)", grid->cols, grid->rows, grid->cell_size);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 0.0f || avail.y <= 0.0f)
    {
        ImGui::End();
        return;
    }

    // Fit the grid into the available space, maintaining aspect ratio.
    const float aspect = static_cast<float>(grid->cols) / static_cast<float>(grid->rows);
    float draw_w = avail.x;
    float draw_h = avail.x / aspect;
    if (draw_h > avail.y)
    {
        draw_h = avail.y;
        draw_w = avail.y * aspect;
    }

    const float cell_px = draw_w / static_cast<float>(grid->cols);

    // Skip per-cell drawing if cells would be sub-pixel.
    if (cell_px < 0.5f)
    {
        ImGui::TextUnformatted("Grid too large to display — zoom in.");
        ImGui::End();
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(origin, ImVec2(origin.x + draw_w, origin.y + draw_h), IM_COL32(20, 20, 20, 255));

    // Cell colors
    constexpr ImU32 kColorBuilding = IM_COL32(200, 80, 80, 255);
    constexpr ImU32 kColorSidewalk = IM_COL32(140, 140, 100, 255);
    constexpr ImU32 kColorRoad = IM_COL32(90, 90, 90, 255);
    constexpr ImU32 kColorPark = IM_COL32(80, 160, 60, 255);

    for (int r = 0; r < grid->rows; ++r)
    {
        for (int c = 0; c < grid->cols; ++c)
        {
            const uint8_t cell = grid->cells[static_cast<size_t>(r) * grid->cols + c];
            if (cell == kCityGridEmpty)
                continue;

            ImU32 color;
            switch (cell)
            {
            case kCityGridBuilding:
                color = kColorBuilding;
                break;
            case kCityGridSidewalk:
                color = kColorSidewalk;
                break;
            case kCityGridRoad:
                color = kColorRoad;
                break;
            case kCityGridPark:
                color = kColorPark;
                break;
            default:
                color = IM_COL32(255, 255, 255, 255);
                break;
            }

            const float x0 = origin.x + c * cell_px;
            const float y0 = origin.y + r * cell_px;
            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cell_px, y0 + cell_px), color);
        }
    }

    auto world_to_screen = [&](const glm::vec2& world) -> ImVec2 {
        const float col = (world.x - grid->origin_x) / grid->cell_size;
        const float row = (world.y - grid->origin_z) / grid->cell_size;
        return {
            origin.x + col * cell_px,
            origin.y + row * cell_px,
        };
    };

    const float route_thickness = std::max(1.5f, cell_px * 0.24f);
    const auto route_segments = build_city_route_render_segments(
        grid->routes,
        grid->cell_size * 0.18f);
    for (const auto& segment : route_segments)
    {
        const ImU32 color = IM_COL32(
            static_cast<int>(segment.color.r * 255.0f),
            static_cast<int>(segment.color.g * 255.0f),
            static_cast<int>(segment.color.b * 255.0f),
            235);
        draw_list->AddLine(
            world_to_screen(segment.a),
            world_to_screen(segment.b),
            color,
            route_thickness);
    }

    // Reserve the space so ImGui layout knows about it.
    ImGui::Dummy(ImVec2(draw_w, draw_h));

    ImGui::End();
}

} // namespace draxul

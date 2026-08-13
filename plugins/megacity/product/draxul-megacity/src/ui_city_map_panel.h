#pragma once

#include <memory>

namespace draxul
{

struct CityGrid;

// Renders a 2D city overview into the current ImGui frame.
// Call once per frame between ImGui::NewFrame() and ImGui::Render().
void render_city_map_panel(const std::shared_ptr<const CityGrid>& grid, bool building_in_progress);

} // namespace draxul

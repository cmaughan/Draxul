#include "ui_panel_style.h"

#include <draxul/perf_timing.h>
#include <imgui.h>

namespace draxul
{

void apply_panel_style()
{
    PERF_MEASURE();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
}

} // namespace draxul

#include "ui_metrics_panel.h"

#include <draxul/perf_timing.h>
#include <draxul/ui_panel.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>

namespace draxul
{

namespace
{

constexpr ImGuiTableFlags kMetricTableFlags = ImGuiTableFlags_SizingStretchProp
    | ImGuiTableFlags_BordersInnerV
    | ImGuiTableFlags_RowBg;

void help_marker(const char* text)
{
    PERF_MEASURE();
    if (!text || !text[0])
        return;

    ImGui::TextDisabled("(?)");
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        return;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool begin_metric_table(const char* id)
{
    PERF_MEASURE();
    if (!ImGui::BeginTable(id, 2, kMetricTableFlags))
        return false;

    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 1.35f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    return true;
}

void metric_label(const char* label, const char* help = nullptr)
{
    PERF_MEASURE();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help)
    {
        ImGui::SameLine();
        help_marker(help);
    }
    ImGui::TableSetColumnIndex(1);
}

} // namespace

void render_window_sections(const PanelLayout& layout, const DiagnosticPanelState& state)
{
    PERF_MEASURE();
    if (ImGui::CollapsingHeader("Help", ImGuiTreeNodeFlags_None))
    {
        ImGui::TextWrapped(
            "This inspector uses dockable windows inside the bottom panel. "
            "Drag tabs to rearrange panes or undock them temporarily while "
            "you inspect rendering and layout state.");
    }

    ImGui::SeparatorText("Dimensions");
    if (begin_metric_table("window_dimensions"))
    {
        metric_label("Window Size", "Current pixel size of the full Draxul window.");
        ImGui::Text("%d x %d px", layout.window_size.x, layout.window_size.y);

        metric_label("Terminal Region", "Height reserved for terminal content after the panel takes its share.");
        ImGui::Text("%d px", layout.terminal_height);

        metric_label("Panel Height", "Height reserved for the diagnostics panel.");
        ImGui::Text("%d px", layout.panel_height);

        metric_label("Panel Origin", "Y coordinate where the panel begins.");
        ImGui::Text("%d px", layout.panel_y);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Grid");
    if (begin_metric_table("window_grid"))
    {
        metric_label("Display PPI", "Detected pixel density for the current monitor.");
        ImGui::Text("%.0f ppi", state.display_ppi);

        metric_label("Cell Size", "Current terminal cell size after font metrics and DPI scaling.");
        ImGui::Text("%d x %d px", state.cell_size.x, state.cell_size.y);

        metric_label("Grid Size", "Active terminal grid in columns and rows.");
        ImGui::Text("%d x %d", state.grid_size.x, state.grid_size.y);
        ImGui::EndTable();
    }
}

void render_renderer_sections(const DiagnosticPanelState& state)
{
    PERF_MEASURE();
    ImGui::SeparatorText("Frame");
    if (begin_metric_table("renderer_frame"))
    {
        metric_label("Last Frame", "Duration of the most recently rendered frame.");
        ImGui::Text("%.2f ms", state.frame_ms);

        metric_label("Rolling Average", "Smoothed frame time across recent frames.");
        ImGui::Text("%.2f ms", state.average_frame_ms);

        metric_label("Dirty Cells", "Cells touched by the last redraw flush.");
        ImGui::Text("%zu", state.dirty_cells);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Atlas");
    ImGui::TextUnformatted("Occupancy");
    ImGui::SameLine();
    help_marker("How full the glyph atlas is right now. Higher values mean Draxul is closer to a rebuild.");
    {
        std::array<char, 32> overlay{};
        const auto usage = std::clamp(state.atlas_usage_ratio, 0.0f, 1.0f);
        std::snprintf(overlay.data(), overlay.size(), "%.1f%%", usage * 100.0f);
        ImGui::ProgressBar(usage, ImVec2(-1.0f, 0.0f), overlay.data());
    }

    if (begin_metric_table("renderer_atlas"))
    {
        metric_label("Glyphs", "Number of glyphs currently packed into the atlas.");
        ImGui::Text("%zu", state.atlas_glyph_count);

        metric_label("Resets", "Number of atlas rebuilds since startup.");
        ImGui::Text("%d", state.atlas_reset_count);
        ImGui::EndTable();
    }
}

void render_startup_sections(const DiagnosticPanelState& state)
{
    PERF_MEASURE();
#ifndef NDEBUG
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
    ImGui::TextUnformatted("DEBUG BUILD — timings are not representative of release performance");
    ImGui::PopStyleColor();
    ImGui::Spacing();
#endif

    if (state.startup_steps.empty())
    {
        ImGui::TextDisabled("No startup timing recorded.");
        return;
    }

    ImGui::SeparatorText("Phases");
    if (ImGui::BeginTable("startup_steps", 2, kMetricTableFlags))
    {
        ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        for (const auto& step : state.startup_steps)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(step.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f ms", step.ms);
        }

        // Total row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Total");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.1f ms", state.startup_total_ms);

        ImGui::EndTable();
    }
}

} // namespace draxul

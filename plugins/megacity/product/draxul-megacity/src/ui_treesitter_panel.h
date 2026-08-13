#pragma once

#include <draxul/megacity_code_config.h>
#include <draxul/treesitter.h>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

struct CodeSemanticSnapshot;
struct SemanticMegacityModel;
struct LiveCityPerfDebugState;

enum class CodeVisualizationPanelMode
{
    City,
    Biology,
};

struct CodeVisualizationPanelCapabilities
{
    bool show_city_build_controls = false;
    bool show_biology_build_controls = false;
    bool show_city_sign_controls = false;
    bool show_city_surface_controls = false;
    bool show_city_preview = false;
    bool show_perf_overlay_controls = false;
    bool show_perf_debug = false;
};

struct CodeVizRendererControls
{
    MegaCityCodeConfig config;
    MegaCityCodeConfig defaults;
    std::vector<std::string> available_modules;
    std::shared_ptr<const LiveCityPerfDebugState> perf_debug;
    bool rebuild_pending = false;
    bool rebuild_requested = false;
    bool reset_camera_requested = false;
    bool committed_edit = false;
    bool set_defaults_requested = false;
};

[[nodiscard]] CodeVisualizationPanelCapabilities code_visualization_panel_capabilities(
    CodeVisualizationPanelMode mode);

// Renders the codebase analysis tree into the current ImGui frame.
// Call once per frame between ImGui::NewFrame() and ImGui::Render().
bool render_treesitter_panel(
    int viewport_x,
    int viewport_y,
    int viewport_w,
    int viewport_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot,
    CodebaseScanProgress scan_progress = {},
    const SemanticMegacityModel* semantic_model = nullptr,
    const CodeSemanticSnapshot* code_semantics = nullptr,
    CodeVizRendererControls* renderer_controls = nullptr,
    CodeVisualizationPanelMode visualization_mode = CodeVisualizationPanelMode::City);

} // namespace draxul

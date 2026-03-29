#pragma once

#include <draxul/megacity_code_config.h>
#include <draxul/treesitter.h>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

struct SemanticMegacityModel;
struct LiveCityPerfDebugState;

struct MegacityRendererControls
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

// Renders the codebase analysis tree into the current ImGui frame.
// Call once per frame between ImGui::NewFrame() and ImGui::Render().
bool render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot,
    const SemanticMegacityModel* semantic_model = nullptr,
    MegacityRendererControls* renderer_controls = nullptr);

} // namespace draxul

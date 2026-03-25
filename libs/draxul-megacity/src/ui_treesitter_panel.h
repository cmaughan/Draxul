#pragma once

#include <draxul/treesitter.h>
#include <memory>

namespace draxul
{

struct MegacityRendererControls
{
    float sign_text_hidden_px = 1.5f;
    float sign_text_full_px = 8.0f;
    float output_gamma = 1.0f;
    bool clamp_semantic_metrics = false;
    bool hide_test_entities = true;
};

// Renders the codebase analysis tree into the current ImGui frame.
// Call once per frame between ImGui::NewFrame() and ImGui::Render().
bool render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot,
    MegacityRendererControls* renderer_controls = nullptr);

} // namespace draxul

#pragma once

#include <draxul/treesitter.h>
#include <memory>

namespace draxul
{

// Renders the codebase analysis tree into the current ImGui frame.
// Call once per frame between ImGui::NewFrame() and ImGui::Render().
void render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot);

} // namespace draxul

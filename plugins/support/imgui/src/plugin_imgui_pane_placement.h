#pragma once

#include <imgui.h>

namespace draxul::plugin_support
{

// Product ImGui frames are pane-local (see PluginImGuiContext::begin_frame).
// While a GPU host draws them, move ImGui's projection so pane coordinate
// (0,0) lands on the pane origin: DisplayPos becomes -origin and DisplaySize
// spans the whole framebuffer, which also offsets every clip rectangle. The
// draw data is restored afterwards so the product's copy is unchanged.
class ScopedPanePlacement
{
public:
    ScopedPanePlacement(ImDrawData& data, int origin_x, int origin_y,
        int framebuffer_width, int framebuffer_height)
        : data_(data)
        , display_pos_(data.DisplayPos)
        , display_size_(data.DisplaySize)
    {
        const ImVec2 scale(
            data.FramebufferScale.x > 0.0f ? data.FramebufferScale.x : 1.0f,
            data.FramebufferScale.y > 0.0f ? data.FramebufferScale.y : 1.0f);
        data.DisplayPos = ImVec2(-static_cast<float>(origin_x) / scale.x,
            -static_cast<float>(origin_y) / scale.y);
        data.DisplaySize = ImVec2(static_cast<float>(framebuffer_width) / scale.x,
            static_cast<float>(framebuffer_height) / scale.y);
    }
    ~ScopedPanePlacement()
    {
        data_.DisplayPos = display_pos_;
        data_.DisplaySize = display_size_;
    }
    ScopedPanePlacement(const ScopedPanePlacement&) = delete;
    ScopedPanePlacement& operator=(const ScopedPanePlacement&) = delete;

private:
    ImDrawData& data_;
    ImVec2 display_pos_;
    ImVec2 display_size_;
};

} // namespace draxul::plugin_support

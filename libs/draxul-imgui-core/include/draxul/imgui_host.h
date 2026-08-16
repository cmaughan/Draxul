#pragma once

struct ImDrawData;
struct ImGuiContext;

namespace draxul
{

class IFrameContext;

// Backend contract for anything that renders ImGui draw data: the core grid
// renderer implements it for the diagnostics panel, and the plugin-support
// GpuImGuiHost implements it inside product modules. This is the single
// definition for the whole tree; it never crosses Draxul's C ABI boundary.
class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;
    virtual bool initialize_imgui_backend() = 0;
    virtual void shutdown_imgui_backend() = 0;
    virtual void rebuild_imgui_font_texture() = 0;
    virtual void begin_imgui_frame() = 0;
    virtual bool render_imgui_draw_data(const ImDrawData*, ImGuiContext*)
    {
        return false;
    }
};

} // namespace draxul

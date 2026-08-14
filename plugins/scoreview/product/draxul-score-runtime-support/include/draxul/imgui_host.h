#pragma once

struct ImDrawData;
struct ImGuiContext;

namespace draxul
{

class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;
    virtual bool initialize_imgui_backend() = 0;
    virtual void shutdown_imgui_backend() = 0;
    virtual void rebuild_imgui_font_texture() = 0;
    virtual void begin_imgui_frame() = 0;
    virtual bool render_imgui_draw_data(
        const ImDrawData*, ImGuiContext*) = 0;
};

} // namespace draxul

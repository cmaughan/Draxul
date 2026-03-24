#pragma once

struct ImDrawData;

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
    virtual void set_imgui_draw_data(const ImDrawData* draw_data) = 0;
};

} // namespace draxul

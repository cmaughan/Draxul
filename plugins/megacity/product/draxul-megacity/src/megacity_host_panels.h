#pragma once

#include <draxul/events.h>
#include <draxul/plugin_runtime.h>
#include <memory>

struct ImGuiContext;

namespace draxul
{

class CodeVizScenePass;
class IImGuiHost;
struct CityGrid;

bool route_megacity_imgui_key(ImGuiContext* context, const KeyEvent& event);
bool route_megacity_imgui_text(ImGuiContext* context, const TextInputEvent& event);
bool route_megacity_imgui_mouse_move(ImGuiContext* context, const MouseMoveEvent& event);
bool route_megacity_imgui_mouse_button(ImGuiContext* context, const MouseButtonEvent& event);
void route_megacity_imgui_mouse_wheel(ImGuiContext* context, const MouseWheelEvent& event);

// Owns ImGui frame/dockspace setup and the module's fixed debug/map panels.
// The host retains only application-specific control result handling.
class MegacityHostPanelFrame
{
public:
    MegacityHostPanelFrame(
        ImGuiContext* context,
        IImGuiHost* backend,
        const PluginRuntimeViewport& viewport,
        int pixel_w,
        int pixel_h,
        float dt,
        bool show_panels);
    ~MegacityHostPanelFrame();

    bool active() const;
    bool panels_visible() const;
    void render_fixed_panels(
        CodeVizScenePass* scene_pass,
        bool biology_view,
        const std::shared_ptr<const CityGrid>& grid,
        bool grid_build_in_progress);
    void finish();

private:
    bool active_ = false;
    bool panels_visible_ = false;
    bool finished_ = false;
};

} // namespace draxul

#include "megacity_host_panels.h"

#include "ui_city_map_panel.h"

#include <draxul/codeviz_scene_pass.h>
#include <draxul/imgui_host.h>
#include <draxul/sdl_imgui_input.h>
#include <imgui.h>

namespace draxul
{

bool route_megacity_imgui_key(ImGuiContext* context, const KeyEvent& event)
{
    if (!context)
        return false;
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (event.mod & kModCtrl) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (event.mod & kModShift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (event.mod & kModAlt) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (event.mod & kModSuper) != 0);
    const ImGuiKey key = sdl_scancode_to_imgui_key(event.scancode);
    if (key != ImGuiKey_None)
        io.AddKeyEvent(key, event.pressed);
    return io.WantCaptureKeyboard;
}

bool route_megacity_imgui_text(ImGuiContext* context, const TextInputEvent& event)
{
    if (!context || event.text.empty())
        return false;
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(event.text.c_str());
    return io.WantTextInput || io.WantCaptureKeyboard;
}

bool route_megacity_imgui_mouse_move(ImGuiContext* context, const MouseMoveEvent& event)
{
    if (!context)
        return false;
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));
    return io.WantCaptureMouse;
}

bool route_megacity_imgui_mouse_button(ImGuiContext* context, const MouseButtonEvent& event)
{
    if (!context)
        return false;
    ImGui::SetCurrentContext(context);
    int button = -1;
    switch (event.button)
    {
    case 1:
        button = 0;
        break;
    case 2:
        button = 2;
        break;
    case 3:
        button = 1;
        break;
    default:
        break;
    }
    if (button >= 0)
        ImGui::GetIO().AddMouseButtonEvent(button, event.pressed);
    return ImGui::GetIO().WantCaptureMouse;
}

void route_megacity_imgui_mouse_wheel(ImGuiContext* context, const MouseWheelEvent& event)
{
    if (!context)
        return;
    ImGui::SetCurrentContext(context);
    ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
}

MegacityHostPanelFrame::MegacityHostPanelFrame(
    ImGuiContext* context,
    IImGuiHost* backend,
    const HostViewport& viewport,
    int pixel_w,
    int pixel_h,
    float dt,
    bool show_panels)
{
    if (!context || !backend)
        return;

    ImGui::SetCurrentContext(context);
    backend->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(viewport.pixel_pos.x + pixel_w),
        static_cast<float>(viewport.pixel_pos.y + pixel_h));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(viewport.pixel_pos.x), static_cast<float>(viewport.pixel_pos.y)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pixel_w), static_cast<float>(pixel_h)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, flags);
    ImGui::PopStyleVar(3);
    const ImGuiID dock_id = ImGui::GetID("MegaCityDock");
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    active_ = true;
    panels_visible_ = show_panels;
}

MegacityHostPanelFrame::~MegacityHostPanelFrame()
{
    finish();
}

bool MegacityHostPanelFrame::active() const
{
    return active_;
}

bool MegacityHostPanelFrame::panels_visible() const
{
    return active_ && panels_visible_;
}

void MegacityHostPanelFrame::render_fixed_panels(
    CodeVizScenePass* scene_pass,
    bool biology_view,
    const std::shared_ptr<const CityGrid>& grid,
    bool grid_build_in_progress)
{
    if (!panels_visible())
        return;
    if (scene_pass)
        scene_pass->render_gbuffer_debug_ui();
    if (!biology_view)
        render_city_map_panel(grid, grid_build_in_progress);
}

void MegacityHostPanelFrame::finish()
{
    if (!active_ || finished_)
        return;
    ImGui::Render();
    finished_ = true;
}

} // namespace draxul

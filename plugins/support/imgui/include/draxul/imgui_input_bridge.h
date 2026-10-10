#pragma once

#include <draxul/events.h>
#include <draxul/plugin_imgui_context.h>
#include <draxul/sdl_imgui_input.h>

#include <imgui.h>

namespace draxul::plugin_support
{

// Routes host input events into a product-owned ImGui context and reports
// ImGui's capture verdict. Every entry point tolerates a null context
// (returning false) and leaves the routed context current, so callers can keep
// querying ImGuiIO afterwards. Pointer events arrive in window pixels; the
// pointer routes take the PluginImGuiContext so they can convert them to the
// pane-local coordinates its frames use.
//
// Header-only on purpose: the event structs resolve against the
// draxul/events.h visible to the including product, which keeps the bridge
// usable from both the bundled build (draxul-types) and the standalone
// product extraction.
class ImGuiInputBridge
{
public:
    // Modifier AddKeyEvent x4 plus the shared scancode table; returns
    // io.WantCaptureKeyboard.
    static bool route_key(ImGuiContext* context, const KeyEvent& event)
    {
        if (context == nullptr)
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

    // Returns io.WantTextInput || io.WantCaptureKeyboard.
    static bool route_text(ImGuiContext* context, const TextInputEvent& event)
    {
        if (context == nullptr || event.text.empty())
            return false;
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharactersUTF8(event.text.c_str());
        return io.WantTextInput || io.WantCaptureKeyboard;
    }

    // Returns io.WantCaptureMouse.
    static bool route_mouse_move(const PluginImGuiContext& imgui, const MouseMoveEvent& event)
    {
        ImGuiContext* context = imgui.context();
        if (context == nullptr)
            return false;
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        add_pane_mouse_pos(imgui, io, event.pos.x, event.pos.y);
        return io.WantCaptureMouse;
    }

    // SDL buttons arrive 1=left, 2=middle, 3=right; ImGui wants 0=left,
    // 1=right, 2=middle. The press position is also queued, so a click with
    // no preceding move still lands where it happened. Returns
    // io.WantCaptureMouse.
    static bool route_mouse_button(const PluginImGuiContext& imgui, const MouseButtonEvent& event)
    {
        ImGuiContext* context = imgui.context();
        if (context == nullptr)
            return false;
        ImGui::SetCurrentContext(context);
        add_pane_mouse_pos(imgui, ImGui::GetIO(), event.pos.x, event.pos.y);
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
        ImGuiIO& io = ImGui::GetIO();
        if (button >= 0)
            io.AddMouseButtonEvent(button, event.pressed);
        return io.WantCaptureMouse;
    }

    // Returns io.WantCaptureMouse.
    static bool route_mouse_wheel(ImGuiContext* context, const MouseWheelEvent& event)
    {
        if (context == nullptr)
            return false;
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseWheelEvent(event.delta.x, event.delta.y);
        return io.WantCaptureMouse;
    }

private:
    static void add_pane_mouse_pos(const PluginImGuiContext& imgui, ImGuiIO& io, int x, int y)
    {
        io.AddMousePosEvent(static_cast<float>(x - imgui.pane_origin_x()),
            static_cast<float>(y - imgui.pane_origin_y()));
    }
};

} // namespace draxul::plugin_support

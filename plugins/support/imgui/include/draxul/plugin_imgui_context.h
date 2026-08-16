#pragma once

#include <draxul/imgui_host.h>

#include <string>

struct ImGuiContext;

namespace draxul::plugin_support
{

// RAII owner of a product plugin's private ImGui context. Products create one
// per runtime, attach the GPU backend their adapter hands them, and drive the
// shared frame-begin; destroy() (or destruction) saves optional ini state,
// shuts the backend down, and destroys the context in the documented order.
//
// The defaults are the product-pane contract shared by every product panel:
// docking enabled, sRGB-aware style (the host swapchain is BGRA8-UNORM with
// sRGB-encoded colors), resize-from-edges, dark style, and no implicit
// imgui.ini (persistence is explicit through Options::ini_path).
class PluginImGuiContext
{
public:
    struct Options
    {
        bool docking_enable = true;
        bool is_srgb = true;
        bool windows_resize_from_edges = true;
        bool windows_move_from_title_bar_only = false;
        // Non-empty: load the window layout from this path in create() and
        // save it back in destroy() (MegaCity's docking persistence).
        std::string ini_path;
    };

    PluginImGuiContext() = default;
    ~PluginImGuiContext();

    PluginImGuiContext(const PluginImGuiContext&) = delete;
    PluginImGuiContext& operator=(const PluginImGuiContext&) = delete;

    // Creates the context, applies the options, and leaves it current. The
    // no-argument overload uses the shared product-pane defaults.
    bool create();
    bool create(const Options& options);

    // Rebuilds the font atlas from a font file (falling back to the ImGui
    // default font) and refreshes the attached backend's font texture.
    void set_font(const std::string& path, float size_pixels);

    // Wires the GPU backend: initializes it against this context and uploads
    // the current font atlas.
    void attach_host(IImGuiHost& host);

    // Shared frame begin: makes the context current, starts the backend
    // frame, sets DisplaySize to viewport origin + size (product panes render
    // in window-global pixels), clamps DeltaTime, and calls ImGui::NewFrame().
    // Returns false while no context or no backend is attached.
    bool begin_frame(int pixel_pos_x, int pixel_pos_y, int pixel_w, int pixel_h,
        float dt);

    // SetCurrentContext → save ini (when configured) → backend shutdown →
    // DestroyContext. Safe to call repeatedly.
    void destroy();

    ImGuiContext* context() const
    {
        return context_;
    }
    IImGuiHost* host() const
    {
        return host_;
    }
    // True once the context exists and a backend is attached.
    bool active() const
    {
        return context_ != nullptr && host_ != nullptr;
    }

private:
    ImGuiContext* context_ = nullptr;
    IImGuiHost* host_ = nullptr;
    std::string ini_path_;
};

} // namespace draxul::plugin_support

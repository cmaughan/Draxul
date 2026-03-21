#pragma once

#include <chrono>
#include <draxul/host.h>
#include <draxul/treesitter.h>
#include <memory>

struct ImGuiContext;

namespace draxul
{

class CubeRenderPass;

// MegaCityHost is a non-terminal I3DHost that renders 3D content directly into
// the GPU render pass. It does not use the grid, text service, or font pipeline.
// Currently renders a spinning cube as a proof-of-concept for the 3D rendering path.
//
// The I3DHost contract means HostManager calls attach_3d_renderer() after
// initialize() succeeds, and detach_3d_renderer() before shutdown(). The cube
// render pass is registered with the renderer in attach_3d_renderer() rather
// than inside initialize(), keeping HostContext free of renderer capability details.
class MegaCityHost final : public I3DHost
{
public:
    MegaCityHost();
    ~MegaCityHost() override;

    bool initialize(const HostContext& context, HostCallbacks callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;

    void set_viewport(const HostViewport& viewport) override;
    void on_font_metrics_changed() override
    {
        // MegaCityHost ignores font metrics changes; it renders 3D geometry, not a text grid.
    }
    void pump() override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_key(const KeyEvent&) override
    {
        // MegaCityHost has no key handling; input is not used in the cube demo.
    }
    void on_text_input(const TextInputEvent&) override
    {
        // MegaCityHost ignores text input events; the 3D cube demo requires no input handling.
    }
    void on_text_editing(const TextEditingEvent&) override
    {
        // MegaCityHost ignores text editing events; the 3D cube demo requires no input handling.
    }
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    // I3DHost
    void attach_3d_renderer(I3DRenderer& renderer) override;
    void detach_3d_renderer() override;
    void attach_imgui_host(IImGuiHost& host) override;
    bool has_imgui() const override
    {
        return imgui_ctx_ != nullptr;
    }
    ImDrawData* render_imgui(float dt) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

private:
    HostCallbacks callbacks_;
    HostViewport viewport_;
    std::shared_ptr<CubeRenderPass> cube_pass_;
    I3DRenderer* renderer_3d_ = nullptr;
    IImGuiHost* imgui_host_ = nullptr;
    ImGuiContext* imgui_ctx_ = nullptr;
    CodebaseScanner scanner_;
    float rotation_angle_ = 0.0f;
    int pixel_w_ = 800;
    int pixel_h_ = 600;
    bool running_ = false;
    std::chrono::steady_clock::time_point last_frame_time_;
};

// Factory function — called from host_factory.cpp
std::unique_ptr<IHost> create_megacity_host();

} // namespace draxul

#pragma once

#include <chrono>
#include <draxul/events.h>
#include <draxul/host_kind.h>
#include <draxul/types.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImDrawData;

namespace draxul
{

class IWindow;
class IGridRenderer;
class IImGuiHost;
class TextService;
class I3DRenderer;

struct HostLaunchOptions
{
    HostKind kind = HostKind::Nvim;
    std::string command;
    std::vector<std::string> args;
    std::string working_dir;
    std::vector<std::string> startup_commands;
    bool enable_ligatures = true;
};

struct HostViewport
{
    int pixel_x = 0;
    int pixel_y = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    int cols = 1;
    int rows = 1;
    int padding = 0;
    float pixel_scale = 1.0f;
};

struct HostDebugState
{
    std::string name;
    int grid_cols = 0;
    int grid_rows = 0;
    size_t dirty_cells = 0;
};

struct HostRuntimeState
{
    bool content_ready = false;
    std::chrono::steady_clock::time_point last_activity_time = std::chrono::steady_clock::now();
};

struct HostCallbacks
{
    std::function<void()> request_frame;
    std::function<void()> request_quit;
    std::function<void()> wake_window;
    std::function<void(const std::string&)> set_window_title;
    std::function<void(int, int, int, int)> set_text_input_area;
};

struct HostContext
{
    IWindow& window;
    IGridRenderer& grid_renderer;
    TextService& text_service;
    HostLaunchOptions launch_options;
    HostViewport initial_viewport;
    float display_ppi = 96.0f;
};

class IHost
{
public:
    virtual ~IHost() = default;

    virtual bool initialize(const HostContext& context, HostCallbacks callbacks) = 0;
    virtual void shutdown() = 0;
    virtual bool is_running() const = 0;
    virtual std::string init_error() const = 0;

    virtual void set_viewport(const HostViewport& viewport) = 0;
    virtual void on_font_metrics_changed() = 0;
    virtual void pump() = 0;
    virtual std::optional<std::chrono::steady_clock::time_point> next_deadline() const = 0;

    virtual void on_key(const KeyEvent& event) = 0;
    virtual void on_text_input(const TextInputEvent& event) = 0;
    virtual void on_text_editing(const TextEditingEvent& event) = 0;
    virtual void on_mouse_button(const MouseButtonEvent& event) = 0;
    virtual void on_mouse_move(const MouseMoveEvent& event) = 0;
    virtual void on_mouse_wheel(const MouseWheelEvent& event) = 0;

    virtual bool dispatch_action(std::string_view action) = 0;
    virtual void request_close() = 0;
    virtual Color default_background() const = 0;
    virtual HostRuntimeState runtime_state() const = 0;
    virtual HostDebugState debug_state() const = 0;
};

std::unique_ptr<IHost> create_host(HostKind kind);

// ---------------------------------------------------------------------------
// I3DHost — hosts that render 3D content directly into the GPU frame.
// HostManager calls attach_3d_renderer() after initialize() succeeds,
// so that 3D capability is wired post-init rather than through HostContext.
// ---------------------------------------------------------------------------
class I3DHost : public IHost
{
public:
    virtual void attach_3d_renderer(I3DRenderer& renderer) = 0;
    virtual void detach_3d_renderer() = 0;

    // Optional ImGui overlay. attach_imgui_host() is called by HostManager after
    // attach_3d_renderer() if an IImGuiHost is available. render_imgui() is called
    // each frame inside begin_frame()/end_frame(); it must call
    // imgui_host.begin_imgui_frame() internally before building the frame.
    // Default implementations are no-ops so existing subclasses need no changes.
    virtual void attach_imgui_host(IImGuiHost& /*host*/) {}
    virtual bool has_imgui() const
    {
        return false;
    }
    virtual ImDrawData* render_imgui(float /*dt*/)
    {
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// IGridHost — marker type for all grid-based (terminal/editor) hosts.
// Inherits I3DHost so that future hosts can optionally register a 3D
// background pass without changing the host manager dispatch.
// ---------------------------------------------------------------------------
class IGridHost : public I3DHost
{
    // No new pure virtuals — subclasses provide no-op attach/detach by default.
};

} // namespace draxul

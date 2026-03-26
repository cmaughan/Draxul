#pragma once

#include <cassert>
#include <chrono>
#include <draxul/events.h>
#include <draxul/host_kind.h>
#include <draxul/types.h>
#include <glm/glm.hpp>
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
struct AppConfig;
class ConfigDocument;

struct HostLaunchOptions
{
    HostKind kind = HostKind::Nvim;
    std::string command;
    std::vector<std::string> args;
    std::string working_dir;
    std::vector<std::string> startup_commands;
    bool enable_ligatures = true;

    // Optional terminal foreground/background colors from config.
    // When set, terminal hosts use these instead of their hardcoded defaults.
    std::optional<Color> terminal_fg;
    std::optional<Color> terminal_bg;
};

struct HostViewport
{
    glm::ivec2 pixel_pos{ 0 };
    glm::ivec2 pixel_size{ 0 };
    glm::ivec2 grid_size{ 1 };
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

class IHostCallbacks
{
public:
    virtual ~IHostCallbacks() = default;
    virtual void request_frame() = 0;
    virtual void request_quit() = 0;
    virtual void wake_window() = 0;
    virtual void set_window_title(const std::string& title) = 0;
    virtual void set_text_input_area(int x, int y, int w, int h) = 0;
};

struct HostContext
{
    HostContext(IWindow* window_in, IGridRenderer* grid_renderer_in, TextService* text_service_in,
        HostLaunchOptions launch_options_in, HostViewport initial_viewport_in, float display_ppi_in)
        : HostContext(window_in, grid_renderer_in, text_service_in, std::move(launch_options_in),
              nullptr, nullptr, std::move(initial_viewport_in), std::weak_ptr<void>{}, display_ppi_in)
    {
    }

    HostContext(IWindow* window_in, IGridRenderer* grid_renderer_in, TextService* text_service_in,
        HostLaunchOptions launch_options_in, HostViewport initial_viewport_in, std::weak_ptr<void> owner_lifetime_in, float display_ppi_in)
        : HostContext(window_in, grid_renderer_in, text_service_in, std::move(launch_options_in),
              nullptr, nullptr, std::move(initial_viewport_in), std::move(owner_lifetime_in), display_ppi_in)
    {
    }

    HostContext(IWindow* window_in, IGridRenderer* grid_renderer_in, TextService* text_service_in,
        AppConfig* config_in, ConfigDocument* config_document_in, HostLaunchOptions launch_options_in, HostViewport initial_viewport_in, float display_ppi_in)
        : HostContext(window_in, grid_renderer_in, text_service_in, std::move(launch_options_in),
              config_in, config_document_in, std::move(initial_viewport_in), std::weak_ptr<void>{}, display_ppi_in)
    {
    }

    HostContext(IWindow* window_in, IGridRenderer* grid_renderer_in, TextService* text_service_in,
        HostLaunchOptions launch_options_in, AppConfig* config_in, ConfigDocument* config_document_in, HostViewport initial_viewport_in,
        std::weak_ptr<void> owner_lifetime_in = {}, float display_ppi_in = 96.0f)
        : window(window_in)
        , grid_renderer(grid_renderer_in)
        , text_service(text_service_in)
        , config(config_in)
        , config_document(config_document_in)
        , launch_options(std::move(launch_options_in))
        , initial_viewport(std::move(initial_viewport_in))
        , track_owner_lifetime(!owner_lifetime_in.expired())
        , owner_lifetime(std::move(owner_lifetime_in))
        , display_ppi(display_ppi_in)
    {
        assert(window != nullptr);
        assert(grid_renderer != nullptr);
        assert(text_service != nullptr);
    }

    IWindow* window = nullptr;
    IGridRenderer* grid_renderer = nullptr;
    TextService* text_service = nullptr;
    AppConfig* config = nullptr;
    ConfigDocument* config_document = nullptr;
    HostLaunchOptions launch_options;
    HostViewport initial_viewport;
    bool track_owner_lifetime = false;
    std::weak_ptr<void> owner_lifetime;
    float display_ppi = 96.0f;
};

class IHost
{
public:
    virtual ~IHost() = default;

    virtual bool initialize(const HostContext& context, IHostCallbacks& callbacks) = 0;
    virtual void shutdown() = 0;
    virtual bool is_running() const = 0;
    virtual std::string init_error() const = 0;

    virtual void set_viewport(const HostViewport& viewport) = 0;
    virtual void on_font_metrics_changed() = 0;
    virtual void pump() = 0;
    virtual std::optional<std::chrono::steady_clock::time_point> next_deadline() const = 0;

    virtual void on_key(const KeyEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }
    virtual void on_text_input(const TextInputEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }
    virtual void on_text_editing(const TextEditingEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }
    virtual void on_mouse_button(const MouseButtonEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }
    virtual void on_mouse_move(const MouseMoveEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }
    virtual void on_mouse_wheel(const MouseWheelEvent& /*event*/)
    {
        // Default no-op; hosts override only the input paths they consume.
    }

    virtual bool dispatch_action(std::string_view action) = 0;
    virtual void request_close() = 0;
    virtual Color default_background() const = 0;
    virtual HostRuntimeState runtime_state() const = 0;
    virtual HostDebugState debug_state() const = 0;

    // Apply a sub-pixel vertical scroll offset to this host's rendered grid.
    // Grid hosts delegate to their IGridHandle; non-grid hosts ignore this.
    virtual void set_scroll_offset(float /*px*/)
    {
        // Default no-op; non-grid hosts ignore scroll offsets.
    }
    virtual bool has_imgui() const
    {
        return false;
    }
    virtual void render_imgui(float /*dt*/)
    {
        // Default no-op; only hosts with extra ImGui chrome override this.
    }
    virtual void set_imgui_font(const std::string& /*path*/, float /*size_pixels*/)
    {
        // Default no-op; only hosts with custom ImGui content need host fonts.
    }
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

    // Optional ImGui contribution. The app owns the ImGui frame lifecycle and
    // calls render_imgui() after it has already begun a frame on the shared
    // application ImGui context. Hosts may add windows or other chrome to the
    // current frame but must not call NewFrame() or Render() themselves.
    virtual void attach_imgui_host(IImGuiHost& /*host*/)
    {
        // Default no-op; hosts with app-level ImGui integration may override this.
    }
};

class IGridHost : public I3DHost
{
    // Marker type only; grid hosts inherit the I3DHost hooks unchanged.
};

} // namespace draxul

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

namespace draxul
{

class IWindow;
class IGridRenderer;
class IImGuiHost;
class IFrameContext;
class TextService;
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

struct HostReloadConfig
{
    bool enable_ligatures = true;
    std::optional<Color> terminal_fg;
    std::optional<Color> terminal_bg;
    float font_size = 11.0f;
    bool smooth_scroll = true;
    float scroll_speed = 1.0f;
    float palette_bg_alpha = 0.9f;
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

    // Dispatch an action to a Neovim host. If one exists, dispatches to it and
    // returns true. If none exists, creates a vertical split with a new NvimHost,
    // dispatches the action, and returns true. Returns false on failure.
    virtual bool dispatch_to_nvim_host(std::string_view /*action*/)
    {
        return false;
    }
};

struct HostContext
{
    IWindow* window = nullptr;
    IGridRenderer* grid_renderer = nullptr;
    TextService* text_service = nullptr;
    AppConfig* config = nullptr;
    ConfigDocument* config_document = nullptr;
    HostLaunchOptions launch_options;
    HostViewport initial_viewport;
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
    virtual void on_font_metrics_changed()
    {
        // Default no-op; only grid-capable hosts need to respond to font metric changes.
    }
    virtual void on_config_reloaded(const HostReloadConfig& /*config*/)
    {
        // Default no-op; only hosts with cached config-driven state override this.
    }
    virtual void pump() = 0;
    virtual void draw(IFrameContext& /*frame*/)
    {
        // Default no-op; only hosts with renderable content override this.
    }
    virtual std::optional<std::chrono::steady_clock::time_point> next_deadline() const = 0;

    virtual void on_focus_gained()
    {
        // Default no-op; hosts override to update visual state (e.g. show cursor).
    }
    virtual void on_focus_lost()
    {
        // Default no-op; hosts override to clear transient input state.
    }

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
    virtual void attach_imgui_host(IImGuiHost& /*host*/)
    {
    }

    // Apply a sub-pixel vertical scroll offset to this host's rendered grid.
    // Grid hosts delegate to their IGridHandle; non-grid hosts ignore this.
    virtual void set_scroll_offset(float /*px*/)
    {
        // Default no-op; non-grid hosts ignore scroll offsets.
    }
    virtual void set_imgui_font(const std::string& /*path*/, float /*size_pixels*/)
    {
        // Default no-op; only hosts with custom ImGui content need host fonts.
    }
};

std::unique_ptr<IHost> create_host(HostKind kind);

} // namespace draxul

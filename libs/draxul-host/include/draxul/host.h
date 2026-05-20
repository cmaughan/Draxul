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
    std::string source_path;
    std::vector<std::string> startup_commands;
    bool enable_ligatures = true;
    std::string pty_capture_file;

    // Optional terminal foreground/background colors from config.
    // When set, terminal hosts use these instead of their hardcoded defaults.
    std::optional<Color> terminal_fg;
    std::optional<Color> terminal_bg;

    // Maximum cells in a single selection (0 = use SelectionManager default).
    int selection_max_cells = 0;
    // Auto-copy to clipboard when a click-drag selection completes.
    bool copy_on_select = true;
    // Minimum line count in a paste payload to trigger user confirmation
    // (0 = no confirmation).
    int paste_confirm_lines = 0;

    // Generic optional host capabilities. Hosts that don't care ignore them.
    // request_continuous_refresh: host wants the renderer presenting every
    //   frame (set when --continuous-refresh is on); animation/3D hosts use
    //   this to know they should mark frames dirty aggressively.
    // show_host_ui_panels: when false, hosts with optional ImGui/debug
    //   overlays should hide them at startup (driven by --no-ui).
    bool request_continuous_refresh = false;
    bool show_host_ui_panels = true;
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
    float markdown_font_size = 11.0f;
    float markdown_margin_columns = 2.0f;
    int selection_max_cells = 65536;
    bool copy_on_select = true;
    int paste_confirm_lines = 0;
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

    // Request that a Markdown source file be opened in a Markdown host.
    // Returns false when the app cannot route or create such a host.
    virtual bool open_markdown_source(std::string_view /*path*/)
    {
        return false;
    }

    // Show a non-blocking toast notification. level: 0=info, 1=warn, 2=error.
    virtual void push_toast(int /*level*/, std::string_view /*message*/) {}
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

    // Capability query: returns true if this host is a Neovim host. Used by
    // App::dispatch_to_nvim_host to locate a target pane without relying on
    // debug-string heuristics. Override in NvimHost to return true.
    virtual bool is_nvim_host() const
    {
        return false;
    }

    // Capability query: returns true if this host can display Markdown sources.
    virtual bool is_markdown_host() const
    {
        return false;
    }

    // One-line status string shown in the per-pane status bar (WI 78). Hosts
    // override to expose host kind, dimensions, cwd, etc. Returning an empty
    // string causes the status bar to display a generic placeholder.
    virtual std::string status_text() const
    {
        return {};
    }
    virtual std::optional<int> exit_code() const
    {
        return std::nullopt;
    }

    // Most recent OSC 7-reported working directory, if any. Returned empty
    // when the host is not a terminal or no cwd has been seen yet. Used by
    // App to default-name workspace tabs from the active pane's cwd.
    virtual std::string current_working_directory() const
    {
        return {};
    }
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

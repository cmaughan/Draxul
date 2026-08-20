#pragma once

// Runtime application options that depend on renderer/window/font subsystem types.
// For pure config data types (AppConfig, AppConfigOverrides, etc.), include
// <draxul/app_config_types.h> instead.

#include <draxul/app_config_types.h>
#include <draxul/host_kind.h>
#include <draxul/renderer.h>
#include <draxul/server_protocol.h>
#include <draxul/window.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

class ClientRecoveryState;
class IHost;

struct AppOptions
{
    // Per-field overrides that take precedence over the loaded AppConfig.
    AppConfigOverrides config_overrides;

    // Runtime / process-lifetime flags -- not persisted to disk.
    bool load_user_config = true;
    bool save_user_config = true;
    bool activate_window_on_startup = true;
    bool start_hidden_window = false;
    bool show_diagnostics_on_startup = false;
    bool show_diagnostics_in_render_test = false;
    // Render scenarios can suppress live CPU/RAM chrome so image comparisons
    // cover product output rather than the machine running the test.
    bool show_system_resources = true;
    bool clamp_window_to_display = true;
    bool show_render_test_window = false;
    // When true, Draxul loads/saves restorable shell-session topology for the
    // selected session id. This is file-backed restore; it does not keep the
    // process alive after the window is closed.
    bool enable_session_restore = false;
    // Expose the local, same-user Session control endpoint. Production
    // desktop launches enable this alongside file-backed Session restore;
    // tests and embedded front-ends opt in explicitly.
    bool enable_control_server = false;
    // UI-local control identity. Shared-server windows use their unique
    // server client id so multiple attached UIs never contend for one Session
    // endpoint; standalone windows use the Session id.
    std::string control_id;
    // Slice 2 only records the experimental server connection. Terminal
    // ownership remains local until the later terminal-runtime slices.
    std::optional<ServerWelcome> server_connection;
    // Shared mutable epoch and per-channel recovery policy for the Session
    // worker and every projected remote terminal in this UI.
    std::shared_ptr<ClientRecoveryState> client_recovery;
    // Slice 6 experimental topology projection. The server owns structural
    // Space/tab/pane values; the UI keeps its active route and presentation.
    bool enable_remote_topology = false;
    std::filesystem::path executable_path;
    std::filesystem::path server_runtime_directory;
    std::string server_client_id;
    // A normal `--plugin` launch creates a server-topology tab before the UI
    // attaches. Focus that durable tab after its first projection; render-test
    // launches bypass shared topology and leave this empty.
    std::string startup_remote_tab_id;
    // Request that the renderer skip vblank waiting so a host can drive
    // continuous refresh (3D scenes, animation-heavy hosts). The host kind
    // doesn't matter — any host that wants to render every frame can opt in.
    bool request_continuous_refresh = false;
    bool no_vblank = false;
    // When true, hosts that have an optional ImGui/debug overlay should hide it
    // on startup. Hosts without such overlays ignore the flag.
    bool hide_host_ui_panels = false;
    // Stable session id for the file-backed shell-session restore flow.
    std::string session_id = "default";
    std::string session_name;
    bool new_session_requested = false;
    bool host_kind_explicit = false;
    std::chrono::milliseconds session_checkpoint_interval = std::chrono::seconds(2);
    std::optional<float> override_display_ppi;
    int render_target_pixel_width = 0;
    int render_target_pixel_height = 0;
    // Default host is the platform's default shell (Zsh on macOS, PowerShell on Windows).
    // Override with --host on the command line.
#ifdef _WIN32
    HostKind host_kind = HostKind::PowerShell;
#else
    HostKind host_kind = HostKind::Zsh;
#endif
    std::string host_command;
    std::vector<std::string> host_args;
    std::string host_source_path;
    std::string host_plugin_id;
    std::string host_plugin_config_json;
    std::vector<std::string> startup_commands;
    std::string host_working_dir;
    std::string pty_capture_file;

    // Optional factory overrides for testing -- leave null in production.
    // window_factory: returns a fully-initialized IWindow. Return nullptr to simulate failure.
    // renderer_create_fn: called instead of create_renderer(); return empty RendererBundle to fail.
    // host_factory: called instead of create_host(). Return nullptr to simulate failure.
    //   The returned IHost must not yet be initialized -- PaneManager calls initialize() itself.
    std::function<std::unique_ptr<IWindow>()> window_factory;
    std::function<RendererBundle(int atlas_size, RendererOptions renderer_options)> renderer_create_fn;
    std::function<std::unique_ptr<IHost>(HostKind)> host_factory;
};

} // namespace draxul

#pragma once
#include "agent_controller.h"
#include "app_shell_layout.h"
#include "command_palette_host.h"
#include "diagnostics_panel_host.h"
#include "frame_timer.h"
#include "gui_action_handler.h"
#include "input_dispatcher.h"
#include "pane_manager.h"
#include "render_tree.h"
#include "session_state.h"
#include "space_controller.h"
#include "toast_host.h"
#include <chrono>
#include <draxul/app_config.h>
#include <draxul/app_options.h>
#include <draxul/config_document.h>
#include <draxul/diagnostics_collector.h>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/result.h>
#include <draxul/system_resource_monitor.h>

#include "weather_service.h"
#include <draxul/text_service.h>
#include <draxul/window.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace draxul
{

class MacOsMenu;
class ControlServer;
class ControlEventJournal;
struct ControlMethodResult;
struct ControlRequest;

// ---------------------------------------------------------------------------
// AppDeps — injectable dependency bundle for App.
//
// Contains factory functions for the key subsystems that App creates during
// initialize().  Passing an AppDeps lets tests (or alternative front-ends)
// supply fakes without touching AppOptions' factory fields.
//
// Use `AppDeps::from_options(opts)` to build an AppDeps from an AppOptions
// using either the caller-supplied factories or the production defaults.
// ---------------------------------------------------------------------------
struct AppDeps
{
    AppOptions options;

    // Factory that creates (but does NOT initialize) the window.
    // Return nullptr to simulate window creation failure.
    std::function<std::unique_ptr<IWindow>()> window_factory;

    // Factory that creates (but does NOT initialize) the renderer bundle.
    // Return an empty RendererBundle to simulate GPU failure.
    std::function<RendererBundle(int atlas_size, RendererOptions renderer_options)> renderer_factory;

    // Factory that creates (but does NOT initialize) the host.
    // Return nullptr to simulate host creation failure.
    std::function<std::unique_ptr<IHost>(HostKind)> host_factory;

    // Optional HTTP transport used by WeatherService. Production leaves this
    // null and uses the platform-native client.
    std::shared_ptr<http::IHttpClient> http_client;

    // Build an AppDeps from an AppOptions, falling back to production
    // defaults for any factory that is not set on the options struct.
    static AppDeps from_options(AppOptions opts);
};

class App : private IHostCallbacks
{
    friend struct AppTestAccess;

public:
    explicit App(AppOptions options = {});
    explicit App(AppDeps deps);
    ~App() override;
    bool initialize();
    void run();
    bool run_smoke_test(std::chrono::milliseconds timeout);
    // Test hook (`--gui-action`): dispatch a canonical GUI action by name.
    bool dispatch_gui_action(std::string_view action);
    std::optional<CapturedFrame> run_screenshot(std::chrono::milliseconds delay);
    std::optional<CapturedFrame> run_render_test(std::chrono::milliseconds timeout,
        std::chrono::milliseconds settle);
    const std::string& last_render_test_error() const
    {
        return last_render_test_error_;
    }
    void shutdown();
    Result<std::string, Error> save_session_as(std::string_view name);
    Result<void, Error> load_session(std::string_view session_id);
    Result<SpaceId, Error> create_space(
        std::string_view name, std::filesystem::path root_directory = {});
    Result<void, Error> activate_space(SpaceId id);
    Result<void, Error> rename_space(SpaceId id, std::string_view name);
    Result<void, Error> close_space(SpaceId id);
    Result<std::string, Error> launch_agent(AgentLaunchRequest request);
    Result<std::string, Error> launch_agent(std::string_view profile_id)
    {
        return launch_agent(AgentLaunchRequest{ .profile_id = std::string(profile_id) });
    }
    SpaceController& space_controller() noexcept
    {
        return space_controller_;
    }
    const SpaceController& space_controller() const noexcept
    {
        return space_controller_;
    }
    const AppShellLayout& shell_layout() const noexcept
    {
        return shell_layout_;
    }
    const std::string& init_error() const
    {
        return last_init_error_;
    }

private:
    bool initialize_text_service();
    bool initialize_chrome_host();
    void wire_window_callbacks();
    void apply_pending_resize();
    // Returns a TextServiceConfig populated from config_. Used by initialize_text_service() and
    // on_display_scale_changed() to avoid duplicating the field assignment at both call sites.
    TextServiceConfig make_text_service_config(const AppConfig& config) const;
    // Applies font metrics from text_service_ to the renderer, diagnostics host, and all hosts.
    // Called after every TextService reinitialisation (startup, DPI change, size change).
    void apply_font_metrics();
    // WI 24: returns a Result so callers (the GUI action handler, tests) can
    // observe failure. Previously this was `void` and silent — the only hint
    // of failure was a log line.
    Result<void, Error> reload_config();

    bool pump_once(std::optional<std::chrono::steady_clock::time_point> wait_deadline = std::nullopt);
    void pump_background_hosts();
    void on_resize(int pixel_w, int pixel_h);
    void on_display_scale_changed(float new_ppi);
    void request_frame() override;
    void request_quit() override;
    void on_window_close_requested();
    void wake_window() override;
    void set_window_title(const std::string& title) override;
    void set_text_input_area(int x, int y, int w, int h) override;
    bool dispatch_to_nvim_host(std::string_view action, bool keep_focus) override;
    bool show_markdown_preview(std::string_view path) override;
    void hide_markdown_preview() override;
    void push_toast(int level, std::string_view message) override;
    void update_diagnostics_panel();
    void refresh_window_layout();
    void refresh_app_shell_layout();
    bool hit_test_app_chrome(int px, int py) const;
    bool hit_test_shell_divider(int px, int py) const;
    void resize_space_sidebar_to_pixel(int px);
    // Converts a PaneDescriptor (pixel region from SplitTree) to a full HostViewport.
    HostViewport viewport_from_descriptor(const PaneDescriptor& desc) const;
    void wire_gui_actions();
    void open_save_session_prompt();
    void open_load_session_picker();
    void open_new_space_prompt();
    void open_switch_space_picker();
    void open_rename_space_prompt();
    void open_launch_agent_prompt();
    void open_attach_agent_picker();
    void open_focus_agent_picker();
    void rebuild_agent_definitions();
    bool close_dead_panes();
    void rebuild_render_tree();
    bool render_frame();
    // The `print_pane` action: request a frame capture, then crop the focused
    // pane, compose an A4 PDF, and hand it to the system print spooler.
    void start_print_focused_pane();
    void finish_print_capture(const CapturedFrame& frame);
    int wait_timeout_ms(std::optional<std::chrono::steady_clock::time_point> wait_deadline) const;
    void refresh_system_resource_snapshot(std::chrono::steady_clock::time_point now);
    // Update tab names from each tab's focused-pane cwd
    // (OSC 7) when the user has not explicitly renamed the tab. Cheap to
    // call every frame — bails out as soon as the cwd basename matches.
    void refresh_tab_default_names();
    bool can_snapshot_session_state() const;
    std::optional<SessionSnapshot> snapshot_session_state() const;
    void mark_session_dirty();
    bool persist_session_state();
    void maybe_checkpoint_session(std::chrono::steady_clock::time_point now);
    bool restore_session_state(int pixel_w, int pixel_h, const SessionSnapshot& state);
    void process_control_requests();
    ControlMethodResult handle_control_request(const ControlRequest& request);

    // --- Tab orchestration (collection ownership lives in TabController) ---
    TabController& active_tab_controller();
    const TabController& active_tab_controller() const;
    PaneManager::Deps make_pane_manager_deps(const Space* space = nullptr);
    bool create_initial_tab(int pixel_w, int pixel_h);
    int add_tab(int pixel_w, int pixel_h, std::optional<HostKind> host_kind = std::nullopt);
    bool close_tab(int tab_id);
    void activate_tab(int tab_id);
    void next_tab();
    void prev_tab();
    void move_tab(int direction); // -1 = left, +1 = right
    void activate_tab_by_index(int one_based_index);
    void activate_pane_by_index(int one_based_index);
    Tab* find_active_tab() noexcept;
    const Tab* find_active_tab() const noexcept;
    Tab& require_active_tab(std::string_view context);
    const Tab& require_active_tab(std::string_view context) const;
    PaneManager& active_pane_manager();
    const PaneManager& active_pane_manager() const;
    const SplitTree& active_tree() const;
    int tab_count() const;
    int active_tab_id() const;

    AppOptions options_;
    // Dependency factories — populated from AppDeps or from AppOptions' factory fields.
    std::function<std::unique_ptr<IWindow>()> window_factory_;
    std::function<RendererBundle(int, RendererOptions)> renderer_factory_;
    std::function<std::unique_ptr<IHost>(HostKind)> host_factory_;

    AppConfig config_;
    ConfigDocument config_document_;
    std::unique_ptr<IWindow> window_;
    RendererBundle renderer_;
    TextService text_service_;

    GuiActionHandler gui_action_handler_{ GuiActionHandler::Deps{} };
    std::unique_ptr<DiagnosticsPanelHost> diagnostics_host_;
    std::unique_ptr<CommandPaletteHost> palette_host_;
    std::unique_ptr<ToastHost> toast_host_;
    std::unique_ptr<IInputRouter> input_router_;
    InputDispatcher input_dispatcher_{ InputDispatcher::Deps{} };
    IWindow::CallbackConnection window_lifecycle_connection_;
#ifdef __APPLE__
    std::unique_ptr<MacOsMenu> macos_menu_;
#endif
    bool init_completed_ = false;
    bool running_ = false;
    bool pending_window_activation_ = true;
    bool saw_frame_ = false;
    bool frame_requested_ = false;
    bool print_capture_pending_ = false;
    PaneDescriptor print_pane_rect_{};
    HostPrintHint print_hint_{};
    int last_pixel_w_ = 0;
    int last_pixel_h_ = 0;
    FrameTimer frame_timer_;
    float display_ppi_ = 96.0f;
    std::shared_ptr<void> host_owner_lifetime_;
    SystemResourceMonitor system_resource_monitor_;
    SystemResourceSnapshot system_resource_snapshot_{};
    WeatherService weather_service_;
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::string last_render_test_error_;
    std::string last_init_error_;
    // Toasts pushed before toast_host_ exists are buffered here and replayed
    // once the host is created during initialize().
    struct PendingInitToast
    {
        int level;
        std::string message;
    };
    std::vector<PendingInitToast> pending_init_toasts_;
    std::unique_ptr<class ChromeHost> chrome_host_;
    AppShellLayout shell_layout_{};
    SpaceController space_controller_;
    AgentController agent_controller_;
    // Drives the Agents rail's visibility. An agent started by hand inside an
    // existing pane produces no pane/tab/Space event, so nothing else would
    // re-run the shell layout to reveal the rail (see pump_once).
    bool last_have_agents_ = false;
    AgentDefinitionRegistry agent_definitions_;
    std::unique_ptr<ControlServer> control_server_;
    std::unique_ptr<ControlEventJournal> control_events_;
    uint64_t next_agent_instance_serial_ = 1;
    RenderNode render_root_;
    std::vector<uint8_t> atlas_upload_scratch_;
    DiagnosticsCollector diagnostics_collector_;
    std::string session_name_;
    bool discard_session_state_on_shutdown_ = false;
    bool session_dirty_ = false;
    uint64_t session_dirty_generation_ = 0;
    std::chrono::steady_clock::time_point last_session_mutation_time_{};
    // Latched so the shell-only persistence gate reports its transitions once
    // instead of silently dropping every checkpoint. Unset until the first
    // evaluation, so an explicit non-shell launch does not warn at startup.
    std::optional<bool> session_persistence_blocked_;
    std::unordered_set<std::string> announced_dead_panes_;
    std::optional<std::pair<int, int>> pending_window_resize_;
};

} // namespace draxul

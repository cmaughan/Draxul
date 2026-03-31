#pragma once
#include "command_palette.h"
#include "frame_timer.h"
#include "gui_action_handler.h"
#include "host_manager.h"
#include "input_dispatcher.h"
#include <chrono>
#include <draxul/app_config.h>
#include <draxul/config_document.h>
#include <draxul/diagnostics_collector.h>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/ui_panel.h>
#include <draxul/window.h>
#include <memory>
#include <optional>
#include <string>

namespace draxul
{

class MacOsMenu;

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

    // Build an AppDeps from an AppOptions, falling back to production
    // defaults for any factory that is not set on the options struct.
    static AppDeps from_options(AppOptions opts);
};

class App : private IHostCallbacks
{
public:
    explicit App(AppOptions options = {});
    explicit App(AppDeps deps);
    ~App();
    bool initialize();
    void run();
    bool run_smoke_test(std::chrono::milliseconds timeout);
    std::optional<CapturedFrame> run_screenshot(std::chrono::milliseconds delay);
    std::optional<CapturedFrame> run_render_test(std::chrono::milliseconds timeout,
        std::chrono::milliseconds settle);
    const std::string& last_render_test_error() const
    {
        return last_render_test_error_;
    }
    void shutdown();
    const std::string& init_error() const
    {
        return last_init_error_;
    }

private:
    bool initialize_text_service();
    bool initialize_host();
    void wire_window_callbacks();
    // Returns a TextServiceConfig populated from config_. Used by initialize_text_service() and
    // on_display_scale_changed() to avoid duplicating the field assignment at both call sites.
    TextServiceConfig make_text_service_config() const;
    // Applies font metrics from text_service_ to the renderer, UI panel, and host.
    // Called after every TextService reinitialisation (startup, DPI change, size change).
    void apply_font_metrics();

    bool pump_once(std::optional<std::chrono::steady_clock::time_point> wait_deadline = std::nullopt);
    void on_resize(int pixel_w, int pixel_h);
    void on_display_scale_changed(float new_ppi);
    void request_frame() override;
    void request_quit() override;
    void wake_window() override;
    void set_window_title(const std::string& title) override;
    void set_text_input_area(int x, int y, int w, int h) override;
    bool dispatch_to_nvim_host(std::string_view action) override;
    void update_diagnostics_panel();
    void refresh_window_layout();
    // Converts a PaneDescriptor (pixel region from SplitTree) to a full HostViewport.
    HostViewport viewport_from_descriptor(const PaneDescriptor& desc) const;
    void wire_gui_actions();
    bool close_dead_panes();
    void render_imgui_overlay(float delta_seconds);
    bool render_frame();
    int wait_timeout_ms(std::optional<std::chrono::steady_clock::time_point> wait_deadline) const;

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
    UiPanel ui_panel_;
    HostManager host_manager_{ HostManager::Deps{} };

    GuiActionHandler gui_action_handler_{ GuiActionHandler::Deps{} };
    CommandPalette command_palette_;
    InputDispatcher input_dispatcher_{ InputDispatcher::Deps{} };
#ifdef __APPLE__
    std::unique_ptr<MacOsMenu> macos_menu_;
#endif
    bool init_completed_ = false;
    bool running_ = false;
    bool pending_window_activation_ = true;
    bool saw_frame_ = false;
    bool frame_requested_ = false;
    int last_pixel_w_ = 0;
    int last_pixel_h_ = 0;
    FrameTimer frame_timer_;
    float display_ppi_ = 96.0f;
    std::shared_ptr<void> host_owner_lifetime_;
    std::chrono::steady_clock::time_point last_panel_frame_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::string last_render_test_error_;
    std::string last_init_error_;
    DiagnosticsCollector diagnostics_collector_;
};

} // namespace draxul

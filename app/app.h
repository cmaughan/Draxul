#pragma once
#include "gui_action_handler.h"
#include "host_manager.h"
#include "input_dispatcher.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <draxul/sdl_window.h>
#include <optional>
#include <string>
#ifdef __APPLE__
// Metal renderer header is internal to draxul-renderer, forward declare
#else
// Vulkan renderer header is internal to draxul-renderer, forward declare
#endif
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/ui_panel.h>

namespace draxul
{

class App
{
public:
    explicit App(AppOptions options = {});
    bool initialize();
    void run();
    bool run_smoke_test(std::chrono::milliseconds timeout);
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::optional<CapturedFrame> run_render_test(std::chrono::milliseconds timeout,
        std::chrono::milliseconds settle);
    const std::string& last_render_test_error() const
    {
        return last_render_test_error_;
    }
#endif
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
    void request_frame();
    void request_quit();
    void update_diagnostics_panel();
    void refresh_window_layout();
    HostViewport current_host_viewport() const;
    int wait_timeout_ms(std::optional<std::chrono::steady_clock::time_point> wait_deadline) const;
    double average_frame_ms() const;

    AppOptions options_;
    AppConfig config_;
    SdlWindow window_;
    RendererBundle renderer_;
    TextService text_service_;
    UiPanel ui_panel_;
    HostManager host_manager_{ HostManager::Deps{} };

    GuiActionHandler gui_action_handler_{ GuiActionHandler::Deps{} };
    InputDispatcher input_dispatcher_{ InputDispatcher::Deps{} };
    bool running_ = false;
    bool pending_window_activation_ = true;
    bool saw_frame_ = false;
    bool frame_requested_ = false;
    double last_frame_ms_ = 0.0;
    std::array<double, 32> recent_frame_ms_ = {};
    size_t recent_frame_ms_count_ = 0;
    size_t recent_frame_ms_index_ = 0;
    float display_ppi_ = 96.0f;
    std::chrono::steady_clock::time_point last_panel_frame_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::string last_render_test_error_;
#endif
    std::string last_init_error_;
    std::vector<StartupStep> startup_steps_;
    double startup_total_ms_ = 0.0;
};

} // namespace draxul

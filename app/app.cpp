#include "app.h"

#include "gui_action_handler.h"
#include "host_manager.h"
#include "input_dispatcher.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <draxul/log.h>
#include <utility>

namespace draxul
{

namespace
{

#ifdef DRAXUL_ENABLE_RENDER_TESTS
void normalize_render_target_window_size(SdlWindow& window, const AppOptions& options)
{
    if (options.render_target_pixel_width <= 0 || options.render_target_pixel_height <= 0)
        return;

    auto [logical_w, logical_h] = window.size_logical();
    auto [pixel_w, pixel_h] = window.size_pixels();
    if (logical_w <= 0 || logical_h <= 0 || pixel_w <= 0 || pixel_h <= 0)
        return;
    if (pixel_w == options.render_target_pixel_width && pixel_h == options.render_target_pixel_height)
        return;

    const int target_logical_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(logical_w) * options.render_target_pixel_width / pixel_w)));
    const int target_logical_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(logical_h) * options.render_target_pixel_height / pixel_h)));

    window.set_size_logical(target_logical_w, target_logical_h);
}
#endif

} // namespace

App::App(AppOptions options)
    : options_(std::move(options))
{
    pending_window_activation_ = options_.activate_window_on_startup;
}

bool App::initialize()
{
    using Clock = std::chrono::steady_clock;
    using Ms = std::chrono::duration<double, std::milli>;

    startup_steps_.clear();
    startup_total_ms_ = 0.0;
    const auto init_start = Clock::now();

    auto time_step = [this](const char* label, auto fn) -> bool {
        const auto t0 = Clock::now();
        const bool ok = fn();
        const double ms = Ms(Clock::now() - t0).count();
        startup_steps_.emplace_back(label, ms);
        startup_total_ms_ += ms;
        return ok;
    };

    bool ok = time_step("Config", [this]() {
        if (options_.load_user_config)
            config_ = AppConfig::load();
        else
            config_ = {};
        apply_overrides(config_, options_.config_overrides);
        return true;
    });

    struct InitRollback
    {
        App* app = nullptr;
        bool armed = true;
        explicit InitRollback(App* a)
            : app(a)
        {
        }
        InitRollback(const InitRollback&) = delete;
        InitRollback& operator=(const InitRollback&) = delete;
        InitRollback(InitRollback&&) = delete;
        InitRollback& operator=(InitRollback&&) = delete;
        ~InitRollback()
        {
            if (armed)
            {
                try
                {
                    app->shutdown();
                }
                catch (...)
                {
                    // Swallow: destructors must not propagate exceptions.
                }
            }
        }
    } rollback(this);

    if (!ok)
        return false;

    if (!time_step("Window Create (SDL)", [this]() {
            window_.set_clamp_to_display(options_.clamp_window_to_display);
#ifdef DRAXUL_ENABLE_RENDER_TESTS
            window_.set_hidden(options_.render_target_pixel_width > 0 && !options_.show_render_test_window);
#endif
            const bool ok = options_.window_init_fn
                ? options_.window_init_fn()
                : window_.initialize("Draxul", config_.window_width, config_.window_height);
            if (!ok)
            {
                last_init_error_ = "Failed to create the application window.";
                return false;
            }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
            normalize_render_target_window_size(window_, options_);
#endif
            return true;
        }))
        return false;

    if (!time_step("Device, Swap, Pipe (GPU)", [this]() {
            renderer_ = options_.renderer_create_fn
                ? options_.renderer_create_fn(config_.atlas_size)
                : create_renderer(config_.atlas_size);
            if (!renderer_ || !renderer_.grid()->initialize(window_))
            {
                last_init_error_ = "Failed to initialize the renderer.";
                return false;
            }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
            if (options_.render_target_pixel_width > 0 && options_.render_target_pixel_height > 0)
                renderer_.grid()->resize(options_.render_target_pixel_width, options_.render_target_pixel_height);
#endif
            return true;
        }))
        return false;

    if (!time_step("Font", [this]() { return initialize_text_service(); }))
        return false;

    if (!time_step("ImGui Setup", [this]() {
            if (!ui_panel_.initialize())
            {
                last_init_error_ = "Failed to initialize the diagnostics panel.";
                return false;
            }
            ui_panel_.set_font(text_service_.primary_font_path(),
                static_cast<float>(text_service_.metrics().cell_height)
                    * static_cast<float>(text_service_.point_size() - 2)
                    / static_cast<float>(text_service_.point_size()));
            ui_panel_.set_visible(options_.show_diagnostics_on_startup);
            refresh_window_layout();
            if (!renderer_.imgui()->initialize_imgui_backend())
            {
                last_init_error_ = "Failed to initialize the renderer ImGui backend.";
                return false;
            }
            return true;
        }))
        return false;

    if (!time_step("Host", [this]() { return initialize_host(); }))
        return false;

    if (host_manager_.host())
        startup_steps_.back().label = "Host (" + host_manager_.host()->debug_state().name + ")";

    startup_total_ms_ = Ms(Clock::now() - init_start).count();

    GuiActionHandler::Deps gui_deps;
    gui_deps.text_service = &text_service_;
    gui_deps.ui_panel = &ui_panel_;
    gui_deps.host = host_manager_.host();
    gui_deps.imgui_host = renderer_.imgui();
    gui_deps.config = &config_;
    // GuiActionHandler already reinitializes TextService; we just apply the resulting metrics.
    // Intentional difference from on_display_scale_changed: no rebuild_imgui_font_texture()
    // needed here because the font size change does not alter the DPI scaling path.
    gui_deps.on_font_changed = [this]() { apply_font_metrics(); };
    gui_deps.on_open_file_dialog = [this]() { window_.show_open_file_dialog(); };
    gui_deps.on_panel_toggled = [this]() {
        refresh_window_layout();
        if (host_manager_.host())
            host_manager_.host()->set_viewport(current_host_viewport());
        update_diagnostics_panel();
        request_frame();
    };
    gui_action_handler_ = GuiActionHandler(std::move(gui_deps));

    wire_window_callbacks();

    saw_frame_ = false;
    frame_requested_ = false;
    last_panel_frame_time_ = std::chrono::steady_clock::now();
    running_ = true;
    rollback.armed = false;
    return true;
}

TextServiceConfig App::make_text_service_config() const
{
    TextServiceConfig text_config;
    text_config.font_path = config_.font_path;
    text_config.bold_font_path = config_.bold_font_path;
    text_config.italic_font_path = config_.italic_font_path;
    text_config.bold_italic_font_path = config_.bold_italic_font_path;
    text_config.fallback_paths = config_.fallback_paths;
    text_config.enable_ligatures = config_.enable_ligatures;
    return text_config;
}

bool App::initialize_text_service()
{
    display_ppi_ = options_.override_display_ppi.value_or(window_.display_ppi());

    const TextServiceConfig text_config = make_text_service_config();
    if (!text_service_.initialize(text_config, config_.font_size, display_ppi_))
    {
        const std::string& attempted = text_config.font_path.empty() ? "(auto-detected)" : text_config.font_path;
        last_init_error_ = "Failed to load the configured font (path: " + attempted
            + "). Check the font_path in config.toml and ensure the file exists.";
        return false;
    }

    // Intentional difference from on_display_scale_changed / on_font_changed: at startup
    // ImGui is not yet initialized, so ui_panel_.set_font(), rebuild_imgui_font_texture(),
    // on_font_metrics_changed(), set_viewport(), and request_frame() are called later.
    const auto& metrics = text_service_.metrics();
    renderer_.grid()->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_.grid()->set_ascender(metrics.ascender);
    refresh_window_layout();
    return true;
}

void App::apply_font_metrics()
{
    const auto& metrics = text_service_.metrics();
    renderer_.grid()->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_.grid()->set_ascender(metrics.ascender);
    ui_panel_.set_font(text_service_.primary_font_path(),
        static_cast<float>(metrics.cell_height)
            * static_cast<float>(text_service_.point_size() - 2)
            / static_cast<float>(text_service_.point_size()));
    if (host_manager_.host())
        host_manager_.host()->on_font_metrics_changed();
    refresh_window_layout();
    if (host_manager_.host())
        host_manager_.host()->set_viewport(current_host_viewport());
    request_frame();
}

bool App::initialize_host()
{
    HostManager::Deps host_deps;
    host_deps.options = &options_;
    host_deps.config = &config_;
    host_deps.window = &window_;
    host_deps.grid_renderer = renderer_.grid();
    host_deps.imgui_host = renderer_.imgui();
    host_deps.text_service = &text_service_;
    host_deps.display_ppi = &display_ppi_;
    host_deps.get_viewport = [this]() { return current_host_viewport(); };
    host_manager_ = HostManager(std::move(host_deps));

    HostCallbacks callbacks;
    callbacks.request_frame = [this]() { request_frame(); };
    callbacks.request_quit = [this]() { request_quit(); };
    callbacks.wake_window = [this]() { window_.wake(); };
    callbacks.set_window_title = [this](const std::string& title) { window_.set_title(title); };
    callbacks.set_text_input_area = [this](int x, int y, int w, int h) { window_.set_text_input_area(x, y, w, h); };

    if (!host_manager_.create(std::move(callbacks)))
    {
        last_init_error_ = host_manager_.error();
        return false;
    }

    if (auto* h3d = dynamic_cast<I3DHost*>(host_manager_.host()))
    {
        const auto& metrics = text_service_.metrics();
        const float font_size = static_cast<float>(metrics.cell_height)
            * static_cast<float>(text_service_.point_size() - 2)
            / static_cast<float>(text_service_.point_size());
        h3d->set_imgui_font(text_service_.primary_font_path(), font_size);
    }

    request_frame();
    return true;
}

void App::wire_window_callbacks()
{
    InputDispatcher::Deps disp_deps;
    disp_deps.keybindings = &config_.keybindings;
    disp_deps.gui_action_handler = &gui_action_handler_;
    disp_deps.ui_panel = &ui_panel_;
    disp_deps.host = host_manager_.host();
    disp_deps.smooth_scroll = config_.smooth_scroll;
    disp_deps.request_frame = [this]() { request_frame(); };
    disp_deps.on_resize = [this](int w, int h) { on_resize(w, h); };
    disp_deps.on_display_scale_changed = [this](float ppi) { on_display_scale_changed(ppi); };
    input_dispatcher_ = InputDispatcher(std::move(disp_deps));
    input_dispatcher_.connect(window_);
}

void App::run()
{
    while (running_)
        pump_once();
}

bool App::run_smoke_test(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        pump_once(deadline);
        if (host_manager_.host() && host_manager_.host()->runtime_state().content_ready && saw_frame_)
            return true;
    }
    return false;
}

#ifdef DRAXUL_ENABLE_RENDER_TESTS
std::optional<CapturedFrame> App::run_render_test(std::chrono::milliseconds timeout, std::chrono::milliseconds settle)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    last_render_test_error_.clear();

    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        pump_once(deadline);

        if (auto captured = renderer_.capture()->take_captured_frame())
            return captured;

        if (!host_manager_.host())
            continue;

        const HostRuntimeState state = host_manager_.host()->runtime_state();
        const auto now = std::chrono::steady_clock::now();
        if (state.content_ready && saw_frame_ && !frame_requested_ && now - state.last_activity_time >= settle)
        {
            renderer_.capture()->request_frame_capture();
            if (renderer_.grid()->begin_frame())
            {
                const float delta_seconds = std::chrono::duration<float>(now - last_panel_frame_time_).count();
                last_panel_frame_time_ = now;
                auto* h3d = dynamic_cast<I3DHost*>(host_manager_.host());
                if (h3d && h3d->has_imgui())
                {
                    renderer_.imgui()->set_imgui_draw_data(
                        h3d->render_imgui(delta_seconds));
                }
                else if (ui_panel_.visible())
                {
                    renderer_.imgui()->begin_imgui_frame();
                    ui_panel_.begin_frame(delta_seconds);
                    renderer_.imgui()->set_imgui_draw_data(ui_panel_.render());
                }
                saw_frame_ = true;
                renderer_.grid()->end_frame();
                if (auto captured = renderer_.capture()->take_captured_frame())
                    return captured;
            }
        }
    }

    last_render_test_error_ = "Timed out waiting for a stable render capture";
    return std::nullopt;
}
#endif

bool App::pump_once(std::optional<std::chrono::steady_clock::time_point> wait_deadline)
{
    while (running_)
    {
        if (pending_window_activation_)
        {
            window_.activate();
            pending_window_activation_ = false;
        }

        if (!window_.poll_events())
        {
            request_quit();
            return false;
        }

        if (!host_manager_.host() || !host_manager_.host()->is_running())
        {
            running_ = false;
            return false;
        }

        host_manager_.host()->pump();
        if (!host_manager_.host()->is_running())
        {
            running_ = false;
            return false;
        }

        if (frame_requested_)
        {
            update_diagnostics_panel();
            {
                const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
                renderer_.grid()->set_scroll_offset(input_dispatcher_.scroll_fraction() * static_cast<float>(ch));
                input_dispatcher_.clear_scroll_event();
            }
            auto frame_start = std::chrono::steady_clock::now();
            if (renderer_.grid()->begin_frame())
            {
                const auto now = std::chrono::steady_clock::now();
                const float delta_seconds = std::chrono::duration<float>(now - last_panel_frame_time_).count();
                last_panel_frame_time_ = now;

                auto* h3d = dynamic_cast<I3DHost*>(host_manager_.host());
                if (h3d && h3d->has_imgui())
                {
                    renderer_.imgui()->set_imgui_draw_data(
                        h3d->render_imgui(delta_seconds));
                }
                else if (ui_panel_.visible())
                {
                    renderer_.imgui()->begin_imgui_frame();
                    ui_panel_.begin_frame(delta_seconds);
                    renderer_.imgui()->set_imgui_draw_data(ui_panel_.render());
                }
                else
                {
                    renderer_.imgui()->set_imgui_draw_data(nullptr);
                }

                saw_frame_ = true;
                renderer_.grid()->end_frame();
                frame_requested_ = false;
                last_frame_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
                recent_frame_ms_[recent_frame_ms_index_] = last_frame_ms_;
                recent_frame_ms_index_ = (recent_frame_ms_index_ + 1) % recent_frame_ms_.size();
                recent_frame_ms_count_ = std::min(recent_frame_ms_count_ + 1, recent_frame_ms_.size());
            }
            return running_;
        }

        if (wait_deadline && std::chrono::steady_clock::now() >= *wait_deadline)
            return running_;

        if (!window_.wait_events(wait_timeout_ms(wait_deadline)))
        {
            request_quit();
            return false;
        }
    }

    return false;
}

void App::on_resize(int pixel_w, int pixel_h)
{
    renderer_.grid()->resize(pixel_w, pixel_h);
    refresh_window_layout();
    if (host_manager_.host())
        host_manager_.host()->set_viewport(current_host_viewport());
    request_frame();
}

void App::on_display_scale_changed(float new_ppi)
{
    if (new_ppi == display_ppi_)
        return;

    display_ppi_ = new_ppi;

    const TextServiceConfig text_config = make_text_service_config();
    if (!text_service_.initialize(text_config, text_service_.point_size(), display_ppi_))
        return;

    // DPI change also requires an ImGui font texture rebuild (different from on_font_changed).
    renderer_.imgui()->rebuild_imgui_font_texture();
    apply_font_metrics();
}

void App::request_frame()
{
    frame_requested_ = true;
    window_.wake();
}

void App::request_quit()
{
    if (host_manager_.host())
        host_manager_.host()->request_close();
    running_ = false;
}

double App::average_frame_ms() const
{
    if (recent_frame_ms_count_ == 0)
        return 0.0;

    double total = 0.0;
    for (size_t i = 0; i < recent_frame_ms_count_; ++i)
        total += recent_frame_ms_[i];
    return total / static_cast<double>(recent_frame_ms_count_);
}

void App::update_diagnostics_panel()
{
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();

    DiagnosticPanelState panel;
    panel.visible = ui_panel_.visible();
    panel.display_ppi = display_ppi_;
    panel.cell_width = cell_w;
    panel.cell_height = cell_h;
    panel.frame_ms = last_frame_ms_;
    panel.average_frame_ms = average_frame_ms();
    panel.atlas_usage_ratio = text_service_.atlas_usage_ratio();
    panel.atlas_glyph_count = text_service_.atlas_glyph_count();
    panel.atlas_reset_count = text_service_.atlas_reset_count();
    panel.startup_steps = startup_steps_;
    panel.startup_total_ms = startup_total_ms_;

    if (host_manager_.host())
    {
        const HostDebugState host_state = host_manager_.host()->debug_state();
        panel.grid_cols = host_state.grid_cols;
        panel.grid_rows = host_state.grid_rows;
        panel.dirty_cells = host_state.dirty_cells;
    }

    ui_panel_.update_diagnostic_state(panel);
}

void App::refresh_window_layout()
{
    auto [pixel_w, pixel_h] = window_.size_pixels();
    const auto [logical_w, logical_h] = window_.size_logical();
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const float pixel_scale = logical_w > 0 ? static_cast<float>(pixel_w) / static_cast<float>(logical_w) : 1.0f;
    (void)logical_h;
    ui_panel_.set_window_metrics(pixel_w, pixel_h, cell_w, cell_h, renderer_.grid()->padding(), pixel_scale);
}

HostViewport App::current_host_viewport() const
{
    const auto& layout = ui_panel_.layout();
    HostViewport viewport;
    viewport.pixel_x = 0;
    viewport.pixel_y = 0;
    viewport.pixel_width = layout.window_width;
    viewport.pixel_height = layout.terminal_height;
    viewport.cols = layout.grid_cols;
    viewport.rows = layout.grid_rows;
    viewport.padding = renderer_.grid()->padding();
    viewport.pixel_scale = layout.pixel_scale;
    return viewport;
}

int App::wait_timeout_ms(std::optional<std::chrono::steady_clock::time_point> wait_deadline) const
{
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (host_manager_.host())
        deadline = host_manager_.host()->next_deadline();
    if (wait_deadline && (!deadline || *wait_deadline < *deadline))
        deadline = wait_deadline;

    if (!deadline)
        return -1;

    const auto now = std::chrono::steady_clock::now();
    if (now >= *deadline)
        return 0;

    return std::max(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count()));
}

void App::shutdown()
{
    if (options_.save_user_config)
    {
        auto [window_w, window_h] = window_.size_logical();
        if (window_w > 0 && window_h > 0)
        {
            config_.window_width = window_w;
            config_.window_height = window_h;
        }
        config_.font_size = text_service_.point_size();
        config_.font_path = text_service_.primary_font_path();
        config_.save();
    }

    host_manager_.shutdown();

    text_service_.shutdown();
    if (renderer_.imgui())
    {
        // Ensure UiPanel's context is current so the backend can clean up its
        // per-context state. MegaCityHost (if active) already shut down its own
        // context in host_manager_.shutdown(), leaving GImGui null.
        ui_panel_.activate_imgui_context();
        renderer_.imgui()->shutdown_imgui_backend();
    }
    ui_panel_.shutdown();
    if (renderer_.grid())
        renderer_.grid()->shutdown();
    window_.shutdown();
}

} // namespace draxul

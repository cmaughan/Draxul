#include "app.h"

#ifdef __APPLE__
#include "macos_menu.h"
#endif
#include "gui_action_handler.h"
#include "host_manager.h"
#include "input_dispatcher.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <draxul/log.h>
#include <draxul/sdl_window.h>
#include <imgui.h>
#include <sstream>
#include <utility>

namespace draxul
{

namespace
{

void normalize_render_target_window_size(IWindow& window, const AppOptions& options)
{
    if (options.render_target_pixel_width <= 0 || options.render_target_pixel_height <= 0)
        return;
    window.normalize_render_target_window_size(options.render_target_pixel_width,
        options.render_target_pixel_height);
}

void render_app_imgui_dockspace(const PanelLayout& layout)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(layout.window_size.x),
        static_cast<float>(layout.window_size.y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##app_dockspace_root", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("DraxulAppDock"),
        ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

} // namespace

App::App(AppOptions options)
    : options_(std::move(options))
{
    pending_window_activation_ = options_.activate_window_on_startup;
}

App::~App() = default;

bool App::initialize()
{
    using Clock = std::chrono::steady_clock;
    using Ms = std::chrono::duration<double, std::milli>;

    diagnostics_collector_.clear_startup_steps();
    const auto init_start = Clock::now();

    auto time_step = [this](const char* label, auto fn) {
        const auto t0 = Clock::now();
        const bool ok = fn();
        const double ms = Ms(Clock::now() - t0).count();
        diagnostics_collector_.record_startup_step(label, ms);
        return ok;
    };

    bool ok = time_step("Config", [this]() {
        if (options_.load_user_config)
        {
            config_ = AppConfig::load();
            config_document_ = ConfigDocument::load();
        }
        else
        {
            config_ = {};
            config_document_ = {};
        }
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
    };
    InitRollback rollback(this);

    if (!ok)
        return false;

    if (!time_step("Window Create (SDL)", [this]() {
            if (options_.window_factory)
            {
                window_ = options_.window_factory();
            }
            else
            {
                auto sdl = std::make_unique<SdlWindow>();
                sdl->set_clamp_to_display(options_.clamp_window_to_display);
#ifdef DRAXUL_ENABLE_RENDER_TESTS
                sdl->set_hidden(options_.render_target_pixel_width > 0 && !options_.show_render_test_window);
#endif
                if (!sdl->initialize("Draxul", config_.window_width, config_.window_height))
                {
                    last_init_error_ = "Failed to create the application window.";
                    return false;
                }
                window_ = std::move(sdl);
            }
            if (!window_)
            {
                last_init_error_ = "Failed to create the application window.";
                return false;
            }
            normalize_render_target_window_size(*window_, options_);
            return true;
        }))
        return false;

    if (!time_step("Device, Swap, Pipe (GPU)", [this]() {
            RendererOptions renderer_options;
            renderer_options.wait_for_vblank = !options_.no_vblank
                && !(options_.host_kind == HostKind::MegaCity && options_.megacity_continuous_refresh);
            renderer_ = options_.renderer_create_fn
                ? options_.renderer_create_fn(config_.atlas_size, renderer_options)
                : create_renderer(config_.atlas_size, renderer_options);
            if (!renderer_ || !renderer_.grid()->initialize(*window_))
            {
                last_init_error_ = "Failed to initialize the renderer.";
                return false;
            }
            if (options_.render_target_pixel_width > 0 && options_.render_target_pixel_height > 0)
                renderer_.grid()->resize(options_.render_target_pixel_width, options_.render_target_pixel_height);
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
                    * (text_service_.point_size() - 2)
                    / text_service_.point_size());
            ui_panel_.set_visible(options_.show_diagnostics_on_startup);
            refresh_window_layout();
            if (!renderer_.imgui() || !renderer_.imgui()->initialize_imgui_backend())
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
        diagnostics_collector_.amend_last_step_label("Host (" + host_manager_.host()->debug_state().name + ")");

    diagnostics_collector_.set_startup_total_ms(Ms(Clock::now() - init_start).count());

    wire_gui_actions();

#ifdef __APPLE__
    macos_menu_ = std::make_unique<MacOsMenu>(gui_action_handler_);
#endif

    wire_window_callbacks();

    // Snapshot the initial window size so the pump loop's size-change check
    // has a correct baseline (avoids a spurious on_resize on the first frame).
    std::tie(last_pixel_w_, last_pixel_h_) = window_->size_pixels();

    saw_frame_ = false;
    last_panel_frame_time_ = std::chrono::steady_clock::now();
    running_ = true;
    // Render one initial composite frame after init so hosts that only request
    // redraws on state changes do not start on a blank window.
    request_frame();
    init_completed_ = true;
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
    display_ppi_ = options_.override_display_ppi.value_or(window_->display_ppi());

    if (const TextServiceConfig text_config = make_text_service_config();
        !text_service_.initialize(text_config, config_.font_size, display_ppi_))
    {
        const std::string& attempted = text_config.font_path.empty() ? "(auto-detected)" : text_config.font_path;
        last_init_error_ = "Failed to load the configured font (path: " + attempted
            + "). Check the font_path in config.toml and ensure the file exists.";
        return false;
    }

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
            * (text_service_.point_size() - 2)
            / text_service_.point_size());
    if (host_manager_.host())
        host_manager_.host()->on_font_metrics_changed();
    refresh_window_layout();
    auto [pixel_w, pixel_h] = window_->size_pixels();
    (void)pixel_h;
    host_manager_.recompute_viewports(pixel_w, ui_panel_.layout().terminal_height);
    request_frame();
}

bool App::initialize_host()
{
    host_owner_lifetime_ = std::make_shared<int>(0);
    HostManager::Deps host_deps;
    host_deps.options = &options_;
    host_deps.config = &config_;
    host_deps.config_document = &config_document_;
    host_deps.window = window_.get();
    host_deps.grid_renderer = renderer_.grid();
    host_deps.imgui_host = renderer_.imgui();
    host_deps.text_service = &text_service_;
    host_deps.display_ppi = &display_ppi_;
    host_deps.owner_lifetime = host_owner_lifetime_;
    host_deps.compute_viewport = [this](const PaneDescriptor& desc) {
        return viewport_from_descriptor(desc);
    };
    host_manager_ = HostManager(std::move(host_deps));

    auto [pixel_w, pixel_h] = window_->size_pixels();
    (void)pixel_h;
    if (!host_manager_.create(*this, pixel_w, ui_panel_.layout().terminal_height))
    {
        last_init_error_ = host_manager_.error();
        return false;
    }

    const auto& metrics = text_service_.metrics();
    const float font_size = static_cast<float>(metrics.cell_height)
        * (text_service_.point_size() - 2)
        / text_service_.point_size();
    host_manager_.host()->set_imgui_font(text_service_.primary_font_path(), font_size);

    request_frame();
    return true;
}

void App::wire_gui_actions()
{
    GuiActionHandler::Deps gui_deps;
    gui_deps.text_service = &text_service_;
    gui_deps.ui_panel = &ui_panel_;
    gui_deps.focused_host = [this]() { return host_manager_.focused_host(); };
    gui_deps.imgui_host = renderer_.imgui();
    gui_deps.config = &config_;
    gui_deps.on_font_changed = [this]() { apply_font_metrics(); };
    gui_deps.on_open_file_dialog = [this]() { window_->show_open_file_dialog(); };
    gui_deps.on_split_vertical = [this]() {
        LeafId new_leaf = host_manager_.split_focused(SplitDirection::Vertical, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(host_manager_.focused_host());
            request_frame();
        }
    };
    gui_deps.on_split_horizontal = [this]() {
        LeafId new_leaf = host_manager_.split_focused(SplitDirection::Horizontal, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(host_manager_.focused_host());
            request_frame();
        }
    };
    gui_deps.on_panel_toggled = [this]() {
        refresh_window_layout();
        auto [pixel_w, pixel_h] = window_->size_pixels();
        (void)pixel_h;
        host_manager_.recompute_viewports(pixel_w, ui_panel_.layout().terminal_height);
        update_diagnostics_panel();
        request_frame();
    };
    gui_action_handler_ = GuiActionHandler(std::move(gui_deps));
}

void App::wire_window_callbacks()
{
    InputDispatcher::Deps disp_deps;
    disp_deps.keybindings = &config_.keybindings;
    disp_deps.gui_action_handler = &gui_action_handler_;
    disp_deps.ui_panel = &ui_panel_;
    disp_deps.host = host_manager_.host();
    disp_deps.host_manager = &host_manager_;
    disp_deps.smooth_scroll = config_.smooth_scroll;
    disp_deps.scroll_speed = config_.scroll_speed;
    {
        auto [pixel_w, pixel_h] = window_->size_pixels();
        auto [logical_w, logical_h] = window_->size_logical();
        (void)pixel_h;
        (void)logical_h;
        disp_deps.pixel_scale = logical_w > 0 ? static_cast<float>(pixel_w) / static_cast<float>(logical_w) : 1.0f;
    }
    disp_deps.request_frame = [this]() { request_frame(); };
    disp_deps.on_resize = [this](int w, int h) { on_resize(w, h); };
    disp_deps.on_display_scale_changed = [this](float ppi) { on_display_scale_changed(ppi); };
    input_dispatcher_ = InputDispatcher(std::move(disp_deps));
    input_dispatcher_.connect(*window_);
}

void App::run()
{
    while (running_)
        pump_once();
}

bool App::run_smoke_test(std::chrono::milliseconds timeout)
{
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + timeout;
    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        pump_once(deadline);
        if (host_manager_.host() && host_manager_.host()->runtime_state().content_ready && saw_frame_)
            return true;
    }
    return false;
}

std::optional<CapturedFrame> App::run_render_test(std::chrono::milliseconds timeout, std::chrono::milliseconds settle)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    last_render_test_error_.clear();
    bool capture_requested = false;
    bool diagnostics_enabled = !options_.show_diagnostics_in_render_test;
    std::optional<std::chrono::steady_clock::time_point> diagnostics_enabled_at;
    std::optional<std::chrono::steady_clock::time_point> ready_since;
    std::optional<std::chrono::steady_clock::time_point> quiet_since;
    if (!renderer_.capture())
    {
        last_render_test_error_ = "Renderer does not support frame capture";
        return std::nullopt;
    }

    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        auto wait_deadline = deadline;
        if (!diagnostics_enabled && ready_since)
            wait_deadline = std::min(wait_deadline, *ready_since + settle);
        if (diagnostics_enabled && !capture_requested)
        {
            if (options_.show_diagnostics_in_render_test && diagnostics_enabled_at)
                wait_deadline = std::min(wait_deadline, *diagnostics_enabled_at + settle);
            else if (!options_.show_diagnostics_in_render_test && quiet_since)
                wait_deadline = std::min(wait_deadline, *quiet_since + settle);
        }

        pump_once(wait_deadline);

        if (auto captured = renderer_.capture()->take_captured_frame())
            return captured;

        if (!host_manager_.host())
            continue;

        const HostRuntimeState state = host_manager_.host()->runtime_state();
        const auto now = std::chrono::steady_clock::now();
        if (state.content_ready && saw_frame_)
        {
            if (!ready_since)
                ready_since = now;
        }
        else
        {
            ready_since.reset();
        }

        if (state.content_ready && saw_frame_ && !frame_requested_)
        {
            if (!quiet_since)
                quiet_since = now;
        }
        else
        {
            quiet_since.reset();
        }

        if (!diagnostics_enabled && ready_since && now - *ready_since >= settle)
        {
            ui_panel_.set_visible(true);
            refresh_window_layout();
            auto [pixel_w, pixel_h] = window_->size_pixels();
            (void)pixel_h;
            host_manager_.recompute_viewports(pixel_w, ui_panel_.layout().terminal_height);
            update_diagnostics_panel();
            request_frame();
            diagnostics_enabled = true;
            diagnostics_enabled_at = now;
            quiet_since.reset();
            continue;
        }

        const bool diagnostics_capture_ready = diagnostics_enabled_at
            && last_panel_frame_time_ > *diagnostics_enabled_at
            && now - *diagnostics_enabled_at >= settle;

        if (!capture_requested && diagnostics_enabled
            && ((options_.show_diagnostics_in_render_test && diagnostics_capture_ready)
                || (!options_.show_diagnostics_in_render_test && quiet_since && now - *quiet_since >= settle)))
        {
            renderer_.capture()->request_frame_capture();
            request_frame();
            capture_requested = true;
        }
    }

    if (host_manager_.host())
    {
        const HostRuntimeState state = host_manager_.host()->runtime_state();
        std::ostringstream oss;
        const bool post_diagnostics_frame = diagnostics_enabled_at && last_panel_frame_time_ > *diagnostics_enabled_at;
        const auto diagnostics_age_ms = diagnostics_enabled_at
            ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *diagnostics_enabled_at).count()
            : -1;
        oss << "Timed out waiting for a stable render capture"
            << " (content_ready=" << (state.content_ready ? "true" : "false")
            << ", saw_frame=" << (saw_frame_ ? "true" : "false")
            << ", frame_requested=" << (frame_requested_ ? "true" : "false")
            << ", diagnostics_enabled=" << (diagnostics_enabled ? "true" : "false")
            << ", capture_requested=" << (capture_requested ? "true" : "false")
            << ", post_diagnostics_frame=" << (post_diagnostics_frame ? "true" : "false")
            << ", diagnostics_age_ms=" << diagnostics_age_ms << ")";
        last_render_test_error_ = oss.str();
    }
    else
    {
        last_render_test_error_ = "Timed out waiting for a stable render capture (no host)";
    }
    return std::nullopt;
}

bool App::close_dead_panes()
{
    std::vector<LeafId> dead;
    host_manager_.for_each_host([&dead](LeafId id, const IHost& h) {
        if (!h.is_running())
            dead.push_back(id);
    });
    for (LeafId id : dead)
    {
        if (host_manager_.host_count() == 1)
        {
            running_ = false;
            return false;
        }
        host_manager_.close_leaf(id);
    }
    return host_manager_.host() != nullptr;
}

void App::render_imgui_overlay(float delta_seconds)
{
    bool any_host_imgui = false;
    host_manager_.for_each_host([&any_host_imgui](LeafId, const IHost& h) {
        if (h.has_imgui())
            any_host_imgui = true;
    });

    if ((ui_panel_.visible() || any_host_imgui) && renderer_.imgui())
    {
        ui_panel_.activate_imgui_context();
        renderer_.imgui()->begin_imgui_frame();
        ui_panel_.begin_frame(delta_seconds);
        render_app_imgui_dockspace(ui_panel_.layout());
        if (ui_panel_.visible())
            ui_panel_.render_into_current_context();
        host_manager_.for_each_host([delta_seconds](LeafId, IHost& h) {
            if (h.has_imgui())
                h.render_imgui(delta_seconds);
        });
        renderer_.imgui()->set_imgui_draw_data(ui_panel_.end_frame());
    }
    else if (renderer_.imgui())
    {
        renderer_.imgui()->set_imgui_draw_data(nullptr);
    }
}

bool App::render_frame()
{
    update_diagnostics_panel();

    const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
    if (auto* host = host_manager_.focused_host())
        host->set_scroll_offset(input_dispatcher_.scroll_fraction() * static_cast<float>(ch));
    input_dispatcher_.clear_scroll_event();

    const auto frame_start = std::chrono::steady_clock::now();
    if (!renderer_.grid()->begin_frame())
        return false;

    const auto now = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(now - last_panel_frame_time_).count();
    last_panel_frame_time_ = now;

    render_imgui_overlay(delta_seconds);

    saw_frame_ = true;
    renderer_.grid()->end_frame();
    frame_requested_ = false;
    frame_timer_.record(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count());
    return true;
}

bool App::pump_once(std::optional<std::chrono::steady_clock::time_point> wait_deadline)
{
    while (running_)
    {
        if (pending_window_activation_)
        {
            window_->activate();
            pending_window_activation_ = false;
        }

        if (!window_->poll_events())
        {
            request_quit();
            return false;
        }

        // Safety net: detect window size changes that SDL may not deliver as
        // events (e.g. during a Windows modal resize drag).
        {
            auto [pw, ph] = window_->size_pixels();
            if (pw != last_pixel_w_ || ph != last_pixel_h_)
                on_resize(pw, ph);
        }

        if (!close_dead_panes())
            return false;
        input_dispatcher_.set_host(host_manager_.focused_host());

        // Pump all hosts
        host_manager_.for_each_host([](LeafId, IHost& h) {
            if (h.is_running())
                h.pump();
        });

        // Re-check after pumping (hosts can die during pump).
        if (!close_dead_panes())
            return false;
        input_dispatcher_.set_host(host_manager_.focused_host());

        if (frame_requested_)
        {
            render_frame();
            return running_;
        }

        if (wait_deadline && std::chrono::steady_clock::now() >= *wait_deadline)
            return running_;

        if (!window_->wait_events(wait_timeout_ms(wait_deadline)))
        {
            request_quit();
            return false;
        }
    }

    return false;
}

void App::on_resize(int pixel_w, int pixel_h)
{
    if (pixel_w == last_pixel_w_ && pixel_h == last_pixel_h_)
        return;
    last_pixel_w_ = pixel_w;
    last_pixel_h_ = pixel_h;
    renderer_.grid()->resize(pixel_w, pixel_h);
    refresh_window_layout();
    host_manager_.recompute_viewports(pixel_w, ui_panel_.layout().terminal_height);
    request_frame();
}

void App::on_display_scale_changed(float new_ppi)
{
    if (new_ppi == display_ppi_)
        return;

    display_ppi_ = new_ppi;

    if (const TextServiceConfig text_config = make_text_service_config();
        !text_service_.initialize(text_config, text_service_.point_size(), display_ppi_))
        return;

    // DPI change also requires an ImGui font texture rebuild (different from on_font_changed).
    if (renderer_.imgui())
        renderer_.imgui()->rebuild_imgui_font_texture();
    apply_font_metrics();

    // Keep the input dispatcher's pixel_scale in sync so mouse hit-testing remains correct.
    auto [pixel_w, pixel_h] = window_->size_pixels();
    auto [logical_w, logical_h] = window_->size_logical();
    (void)pixel_h;
    (void)logical_h;
    if (logical_w > 0)
        input_dispatcher_.set_pixel_scale(static_cast<float>(pixel_w) / static_cast<float>(logical_w));
}

void App::request_frame()
{
    frame_requested_ = true;
    wake_window();
}

void App::request_quit()
{
    host_manager_.for_each_host([](LeafId, IHost& h) {
        h.request_close();
    });
    running_ = false;
}

void App::wake_window()
{
    if (window_)
        window_->wake();
}

void App::set_window_title(const std::string& title)
{
    if (window_)
        window_->set_title(title);
}

void App::set_text_input_area(int x, int y, int w, int h)
{
    if (window_)
        window_->set_text_input_area(x, y, w, h);
}

void App::update_diagnostics_panel()
{
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();

    DiagnosticPanelState panel;
    panel.visible = ui_panel_.visible();
    panel.display_ppi = display_ppi_;
    panel.cell_size = { cell_w, cell_h };
    panel.frame_ms = frame_timer_.last_ms();
    panel.average_frame_ms = frame_timer_.average_ms();
    panel.atlas_usage_ratio = text_service_.atlas_usage_ratio();
    panel.atlas_glyph_count = text_service_.atlas_glyph_count();
    panel.atlas_reset_count = text_service_.atlas_reset_count();
    panel.startup_steps = diagnostics_collector_.startup_steps();
    panel.startup_total_ms = diagnostics_collector_.startup_total_ms();

    if (host_manager_.host())
    {
        const HostDebugState host_state = host_manager_.host()->debug_state();
        panel.grid_size = { host_state.grid_cols, host_state.grid_rows };
        panel.dirty_cells = host_state.dirty_cells;
    }

    ui_panel_.update_diagnostic_state(panel);
}

void App::refresh_window_layout()
{
    auto [pixel_w, pixel_h] = window_->size_pixels();
    const auto [logical_w, logical_h] = window_->size_logical();
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const float pixel_scale = logical_w > 0 ? static_cast<float>(pixel_w) / static_cast<float>(logical_w) : 1.0f;
    (void)logical_h;
    ui_panel_.set_window_metrics(pixel_w, pixel_h, cell_w, cell_h, renderer_.grid()->padding(), pixel_scale);
}

HostViewport App::viewport_from_descriptor(const PaneDescriptor& desc) const
{
    const int padding = renderer_.grid()->padding();
    const auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const auto& layout = ui_panel_.layout();

    HostViewport viewport;
    viewport.pixel_pos = desc.pixel_pos;
    viewport.pixel_size = desc.pixel_size;
    viewport.padding = padding;
    viewport.pixel_scale = layout.pixel_scale;

    const int usable_w = viewport.pixel_size.x - 2 * padding;
    const int usable_h = viewport.pixel_size.y - 2 * padding;
    viewport.grid_size.x = cell_w > 0 ? std::max(1, usable_w / cell_w) : 1;
    viewport.grid_size.y = cell_h > 0 ? std::max(1, usable_h / cell_h) : 1;
    return viewport;
}

int App::wait_timeout_ms(std::optional<std::chrono::steady_clock::time_point> wait_deadline) const
{
    // Cap the wait so that output from a background reader thread is displayed
    // promptly even if SDL_PushEvent does not reliably wake SDL_WaitEvent on
    // every platform (observed on macOS with SDL 3.2.x when the reader thread's
    // wakeup event fires between SDL_PeepEvents and the platform wait entry).
    static constexpr int kHostPollIntervalMs = 50;

    std::optional<std::chrono::steady_clock::time_point> deadline;
    bool any_host_running = false;
    host_manager_.for_each_host([&deadline, &any_host_running](LeafId, const IHost& host) {
        if (host.is_running())
            any_host_running = true;
        auto d = host.next_deadline();
        if (d && (!deadline || *d < *deadline))
            deadline = d;
    });
    if (wait_deadline && (!deadline || *wait_deadline < *deadline))
        deadline = wait_deadline;

    if (!deadline)
        return any_host_running ? kHostPollIntervalMs : -1;

    const auto now = std::chrono::steady_clock::now();
    if (now >= *deadline)
        return 0;

    int ms = std::max(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count()));
    if (any_host_running)
        ms = std::min(ms, kHostPollIntervalMs);
    return ms;
}

void App::shutdown()
{
#ifdef __APPLE__
    macos_menu_.reset(); // tear down menu before handler goes away
#endif

    host_manager_.shutdown();
    host_owner_lifetime_.reset();

    if (options_.save_user_config && init_completed_)
    {
        init_completed_ = false; // prevent double-save on repeated shutdown() calls
        auto [window_w, window_h] = window_->size_logical();
        if (window_w > 0 && window_h > 0)
        {
            config_.window_width = window_w;
            config_.window_height = window_h;
        }
        config_.font_size = text_service_.point_size();
        config_.font_path = text_service_.primary_font_path();
        config_document_.merge_core_config(config_);
        config_document_.save();
    }

    text_service_.shutdown();
    if (renderer_.imgui())
    {
        ui_panel_.activate_imgui_context();
        renderer_.imgui()->shutdown_imgui_backend();
    }
    ui_panel_.shutdown();
    if (renderer_.grid())
        renderer_.grid()->shutdown();
    if (window_)
        window_->shutdown();
}

} // namespace draxul

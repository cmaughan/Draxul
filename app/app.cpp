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
#include <cmath>
#include <cstring>
#include <draxul/grid_host_base.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/pixel_scale.h>
#include <draxul/sdl_window.h>
#include <imgui.h>
#include <sstream>
#include <utility>

namespace draxul
{

namespace
{

// State machine for run_render_test(). Replaces the previous set of scattered optional flags
// with an explicit enum + context struct so the current phase is always unambiguous.
enum class RenderTestPhase
{
    kWaitingForContent, // Host has not yet reported content_ready + a rendered frame.
    kSettlingContent, // Content is ready; waiting for the settle period to elapse.
    kEnablingDiagnostics, // Diagnostics panel just turned on; waiting for first panel frame + settle.
    kSettlingForCapture, // Diagnostics done (or skipped); quiet period before capture.
    kCapturing, // Frame capture requested; waiting for the GPU readback.
};

const char* render_test_phase_name(RenderTestPhase p)
{
    switch (p)
    {
    case RenderTestPhase::kWaitingForContent:
        return "WaitingForContent";
    case RenderTestPhase::kSettlingContent:
        return "SettlingContent";
    case RenderTestPhase::kEnablingDiagnostics:
        return "EnablingDiagnostics";
    case RenderTestPhase::kSettlingForCapture:
        return "SettlingForCapture";
    case RenderTestPhase::kCapturing:
        return "Capturing";
    }
    return "Unknown";
}

struct RenderTestContext
{
    RenderTestPhase phase = RenderTestPhase::kWaitingForContent;

    // Timestamp of the event that starts the current settle window.
    std::chrono::steady_clock::time_point settle_start{};

    // True once content has been observed "quiet" (ready + no pending frames) in
    // the kSettlingForCapture phase. Reset when quiet is lost so the settle
    // timer restarts on the next quiet observation.
    bool quiet_observed = false;

    // When diagnostics were enabled (for the timeout diagnostic message).
    std::optional<std::chrono::steady_clock::time_point> diagnostics_enabled_at;
};

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

AppDeps AppDeps::from_options(AppOptions opts)
{
    AppDeps deps;
    deps.window_factory = opts.window_factory;
    deps.renderer_factory = opts.renderer_create_fn;
    deps.host_factory = opts.host_factory;
    deps.options = std::move(opts);
    return deps;
}

App::App(AppOptions options)
    : App(AppDeps::from_options(std::move(options)))
{
}

App::App(AppDeps deps)
    : options_(std::move(deps.options))
    , window_factory_(std::move(deps.window_factory))
    , renderer_factory_(std::move(deps.renderer_factory))
    , host_factory_(std::move(deps.host_factory))
{
    // HostManager reads options_.host_factory to create hosts.  Sync our
    // canonical factory back so the two sources stay consistent.
    options_.host_factory = host_factory_;
    pending_window_activation_ = options_.activate_window_on_startup;
}

App::~App() = default;

bool App::initialize()
{
    PERF_MEASURE();
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
            if (window_factory_)
            {
                window_ = window_factory_();
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
            renderer_ = renderer_factory_
                ? renderer_factory_(config_.atlas_size, renderer_options)
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    host_manager_.recompute_viewports(window_->width_pixels(), ui_panel_.layout().terminal_height);
    request_frame();
}

bool App::initialize_host()
{
    PERF_MEASURE();
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

    if (!host_manager_.create(*this, window_->width_pixels(), ui_panel_.layout().terminal_height))
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
    PERF_MEASURE();
    GuiActionHandler::Deps gui_deps;
    gui_deps.text_service = &text_service_;
    gui_deps.ui_panel = &ui_panel_;
    gui_deps.focused_host = [this]() { return host_manager_.focused_host(); };
    gui_deps.imgui_host = renderer_.imgui();
    gui_deps.config = &config_;
    gui_deps.on_font_changed = [this]() { apply_font_metrics(); };
    gui_deps.on_open_file_dialog = [this]() { window_->show_open_file_dialog(); };
    gui_deps.on_split_vertical = [this](std::optional<HostKind> kind) {
        LeafId new_leaf = kind
            ? host_manager_.split_focused(SplitDirection::Vertical, *kind, *this)
            : host_manager_.split_focused(SplitDirection::Vertical, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(host_manager_.focused_host());
            request_frame();
        }
    };
    gui_deps.on_split_horizontal = [this](std::optional<HostKind> kind) {
        LeafId new_leaf = kind
            ? host_manager_.split_focused(SplitDirection::Horizontal, *kind, *this)
            : host_manager_.split_focused(SplitDirection::Horizontal, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(host_manager_.focused_host());
            request_frame();
        }
    };
    gui_deps.on_panel_toggled = [this]() {
        refresh_window_layout();
        host_manager_.recompute_viewports(window_->width_pixels(), ui_panel_.layout().terminal_height);
        update_diagnostics_panel();
        request_frame();
    };
    gui_deps.on_command_palette = [this]() {
        if (command_palette_.is_open())
            command_palette_.close();
        else
            command_palette_.open();
        request_frame();
    };
    gui_deps.on_edit_config = [this]() {
        HostLaunchOptions launch;
        launch.kind = HostKind::Nvim;
        launch.args = { ConfigDocument::default_path().string() };
        LeafId new_leaf = host_manager_.split_focused(SplitDirection::Vertical, std::move(launch), *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(host_manager_.focused_host());
            request_frame();
        }
    };
    gui_action_handler_ = GuiActionHandler(std::move(gui_deps));

    // Wire command palette deps (must be after gui_action_handler_ is constructed)
    CommandPalette::Deps palette_deps;
    palette_deps.gui_action_handler = &gui_action_handler_;
    palette_deps.keybindings = &config_.keybindings;
    palette_deps.request_frame = [this]() { request_frame(); };
    command_palette_ = CommandPalette(std::move(palette_deps));
}

void App::wire_window_callbacks()
{
    PERF_MEASURE();
    InputDispatcher::Deps disp_deps;
    disp_deps.keybindings = &config_.keybindings;
    disp_deps.gui_action_handler = &gui_action_handler_;
    disp_deps.command_palette = &command_palette_;
    disp_deps.ui_panel = &ui_panel_;
    disp_deps.host = host_manager_.host();
    disp_deps.host_manager = &host_manager_;
    disp_deps.smooth_scroll = config_.smooth_scroll;
    disp_deps.scroll_speed = config_.scroll_speed;
    disp_deps.pixel_scale = PixelScale::from_window(window_->width_pixels(), window_->width_logical());
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
    PERF_MEASURE();
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

std::optional<CapturedFrame> App::run_screenshot(std::chrono::milliseconds delay)
{
    PERF_MEASURE();
    if (!renderer_.capture())
        return std::nullopt;

    const auto start = std::chrono::steady_clock::now();
    const auto capture_time = start + delay;
    const auto deadline = capture_time + std::chrono::seconds(10);
    bool capture_requested = false;

    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        // Keep requesting frames so the main loop doesn't sleep in wait_events.
        request_frame();

        const auto next_wake = std::min(deadline,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
        pump_once(next_wake);

        if (auto captured = renderer_.capture()->take_captured_frame())
            return captured;

        if (!capture_requested && std::chrono::steady_clock::now() >= capture_time)
        {
            renderer_.capture()->request_frame_capture();
            request_frame();
            capture_requested = true;
        }
    }
    return std::nullopt;
}

std::optional<CapturedFrame> App::run_render_test(std::chrono::milliseconds timeout, std::chrono::milliseconds settle)
{
    PERF_MEASURE();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    last_render_test_error_.clear();

    if (!renderer_.capture())
    {
        last_render_test_error_ = "Renderer does not support frame capture";
        return std::nullopt;
    }

    // When show_diagnostics_in_render_test is false we skip the diagnostics
    // phase entirely and jump straight to settling for capture once content is
    // ready. When true, the diagnostics panel is enabled after the initial
    // settle and we wait for an additional settle before capturing.
    const bool want_diagnostics = options_.show_diagnostics_in_render_test;

    RenderTestContext ctx;
    // If diagnostics are not wanted, we still need to eventually capture. The
    // flow is: WaitingForContent -> SettlingForCapture -> Capturing.
    // If diagnostics ARE wanted: WaitingForContent -> SettlingContent ->
    // EnablingDiagnostics -> SettlingForCapture -> Capturing.

    while (running_ && std::chrono::steady_clock::now() < deadline)
    {
        // Compute a tight wait_deadline so pump_once wakes when the current
        // settle window expires rather than sleeping until the outer deadline.
        auto wait_deadline = deadline;
        if (ctx.phase == RenderTestPhase::kSettlingContent
            || ctx.phase == RenderTestPhase::kEnablingDiagnostics
            || ctx.phase == RenderTestPhase::kSettlingForCapture)
        {
            wait_deadline = std::min(wait_deadline, ctx.settle_start + settle);
        }

        pump_once(wait_deadline);

        // Check for a completed capture before anything else — if the GPU
        // readback finished we are done regardless of phase.
        if (auto captured = renderer_.capture()->take_captured_frame())
            return captured;

        if (!host_manager_.host())
            continue;

        const HostRuntimeState host_state = host_manager_.host()->runtime_state();
        const auto now = std::chrono::steady_clock::now();
        const bool content_ready = host_state.content_ready && saw_frame_;
        const bool content_quiet = content_ready && !frame_requested_;

        switch (ctx.phase)
        {
        case RenderTestPhase::kWaitingForContent:
        {
            if (content_ready)
            {
                ctx.settle_start = now;
                ctx.phase = RenderTestPhase::kSettlingContent;
            }
            break;
        }

        case RenderTestPhase::kSettlingContent:
        {
            if (!content_ready)
            {
                // Content lost — go back and wait again.
                ctx.phase = RenderTestPhase::kWaitingForContent;
                break;
            }
            if (now - ctx.settle_start >= settle)
            {
                if (want_diagnostics)
                {
                    // Enable the diagnostics panel and wait for it to render.
                    ui_panel_.set_visible(true);
                    refresh_window_layout();
                    host_manager_.recompute_viewports(
                        window_->width_pixels(), ui_panel_.layout().terminal_height);
                    update_diagnostics_panel();
                    request_frame();
                    ctx.diagnostics_enabled_at = now;
                    ctx.settle_start = now;
                    ctx.phase = RenderTestPhase::kEnablingDiagnostics;
                }
                else
                {
                    // No diagnostics — go straight to settling for capture.
                    // Reset settle_start since we need to observe quiet from now.
                    if (content_quiet)
                        ctx.settle_start = now;
                    ctx.phase = RenderTestPhase::kSettlingForCapture;
                }
            }
            break;
        }

        case RenderTestPhase::kEnablingDiagnostics:
        {
            // Wait for the diagnostics panel to actually render (panel frame
            // time must advance past the enable timestamp) AND for the settle
            // period to elapse.
            const bool panel_rendered = last_panel_frame_time_ > *ctx.diagnostics_enabled_at;
            if (panel_rendered && now - ctx.settle_start >= settle)
            {
                // Diagnostics panel is stable — request capture.
                renderer_.capture()->request_frame_capture();
                request_frame();
                ctx.phase = RenderTestPhase::kCapturing;
            }
            break;
        }

        case RenderTestPhase::kSettlingForCapture:
        {
            // In the non-diagnostics path we need content to be "quiet"
            // (ready + no pending frames) for the settle period.
            if (!content_quiet)
            {
                // Not quiet -- reset so the settle timer restarts when quiet resumes.
                ctx.quiet_observed = false;
                break;
            }
            if (!ctx.quiet_observed)
            {
                // First iteration where content is quiet -- start the settle timer.
                ctx.quiet_observed = true;
                ctx.settle_start = now;
                break;
            }
            if (now - ctx.settle_start >= settle)
            {
                renderer_.capture()->request_frame_capture();
                request_frame();
                ctx.phase = RenderTestPhase::kCapturing;
            }
            break;
        }

        case RenderTestPhase::kCapturing:
            // Waiting for take_captured_frame() at the top of the loop.
            break;
        }
    }

    // Timeout — build a diagnostic error message.
    if (host_manager_.host())
    {
        const HostRuntimeState host_state = host_manager_.host()->runtime_state();
        const bool post_diagnostics_frame = ctx.diagnostics_enabled_at
            && last_panel_frame_time_ > *ctx.diagnostics_enabled_at;
        const auto diagnostics_age_ms = ctx.diagnostics_enabled_at
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - *ctx.diagnostics_enabled_at)
                  .count()
            : -1;
        std::ostringstream oss;
        oss << "Timed out waiting for a stable render capture"
            << " (phase=" << render_test_phase_name(ctx.phase)
            << ", content_ready=" << (host_state.content_ready ? "true" : "false")
            << ", saw_frame=" << (saw_frame_ ? "true" : "false")
            << ", frame_requested=" << (frame_requested_ ? "true" : "false")
            << ", diagnostics_enabled=" << (ctx.diagnostics_enabled_at.has_value() ? "true" : "false")
            << ", capture_requested="
            << (ctx.phase == RenderTestPhase::kCapturing ? "true" : "false")
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
    PERF_MEASURE();
    std::vector<LeafId> dead;
    host_manager_.for_each_host([&dead](LeafId id, const IHost& h) {
        if (!h.is_running())
            dead.push_back(id);
    });
    if (!dead.empty())
    {
        // Clear the input dispatcher's host pointer before destroying panes so
        // set_host() can't call on_focus_lost() on a dangling pointer.
        input_dispatcher_.set_host(nullptr);
    }
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
    PERF_MEASURE();
    bool any_host_imgui = false;
    host_manager_.for_each_host([&any_host_imgui](LeafId, const IHost& h) {
        if (h.has_imgui())
            any_host_imgui = true;
    });

    const bool need_imgui = ui_panel_.visible() || any_host_imgui;
    if (need_imgui && renderer_.imgui())
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

    // Render command palette as overlay cells (no ImGui).
    if (auto* host = dynamic_cast<GridHostBase*>(host_manager_.focused_host()))
    {
        if (command_palette_.is_open())
        {
            auto vs = command_palette_.view_state(host->grid_cols(), host->grid_rows(), config_.palette_bg_alpha);
            auto overlay = gui::render_palette(vs, text_service_);
            host->set_overlay_cells(overlay);

            // render_palette() may rasterize new glyphs (e.g. the full-block used
            // for panel occlusion) after GridRenderingPipeline already uploaded and
            // cleared the atlas. Flush any new dirty region to the GPU now so the
            // draw calls this frame sample correct atlas data.
            if (text_service_.atlas_dirty())
            {
                const auto dirty = text_service_.atlas_dirty_rect();
                if (dirty.size.x > 0 && dirty.size.y > 0)
                {
                    constexpr size_t kPixelSize = 4;
                    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
                    std::vector<uint8_t> scratch(row_bytes * dirty.size.y);
                    const uint8_t* atlas = text_service_.atlas_data();
                    const int atlas_w = text_service_.atlas_width();
                    for (int r = 0; r < dirty.size.y; ++r)
                    {
                        const uint8_t* src = atlas
                            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
                        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
                    }
                    renderer_.grid()->update_atlas_region(
                        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
                }
                text_service_.clear_atlas_dirty();
            }
        }
        else if (palette_was_open_)
        {
            host->set_overlay_cells({});
        }
    }
    palette_was_open_ = command_palette_.is_open();
}

bool App::render_frame()
{
    PERF_MEASURE();
    // Consume the current request up front so any nested request_frame() calls
    // made during this frame schedule a follow-up frame instead of being
    // cleared at the end of the render.
    frame_requested_ = false;

    update_diagnostics_panel();

    const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
    if (auto* host = host_manager_.focused_host())
        host->set_scroll_offset(input_dispatcher_.scroll_fraction() * static_cast<float>(ch));
    input_dispatcher_.clear_scroll_event();

    const auto frame_start = std::chrono::steady_clock::now();
    if (!renderer_.grid()->begin_frame())
    {
        runtime_perf_collector().cancel_frame();
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(now - last_panel_frame_time_).count();
    last_panel_frame_time_ = now;

    render_imgui_overlay(delta_seconds);

    saw_frame_ = true;
    renderer_.grid()->end_frame();
    runtime_perf_collector().end_frame();
    frame_timer_.record(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count());
    return true;
}

bool App::pump_once(std::optional<std::chrono::steady_clock::time_point> wait_deadline)
{
    PERF_MEASURE();
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

        runtime_perf_collector().begin_frame();

        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(host_manager_.focused_host());

        // Pump all hosts
        host_manager_.for_each_host([](LeafId, IHost& h) {
            if (h.is_running())
                h.pump();
        });

        // Re-check after pumping (hosts can die during pump).
        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(host_manager_.focused_host());

        if (frame_requested_)
        {
            render_frame();
            return running_;
        }

        runtime_perf_collector().cancel_frame();

        if (wait_deadline && std::chrono::steady_clock::now() >= *wait_deadline)
            return running_;

        if (!window_->wait_events(wait_timeout_ms(wait_deadline)))
        {
            runtime_perf_collector().cancel_frame();
            request_quit();
            return false;
        }
    }

    return false;
}

void App::on_resize(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    if (std::abs(new_ppi - display_ppi_) < 0.5f)
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
    input_dispatcher_.set_pixel_scale(PixelScale::from_window(window_->width_pixels(), window_->width_logical()));
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

bool App::dispatch_to_nvim_host(std::string_view action)
{
    // Find an existing NvimHost (identified by debug name, no side effects).
    IHost* nvim_host = nullptr;
    LeafId nvim_leaf = kInvalidLeaf;
    host_manager_.for_each_host([&](LeafId id, IHost& host) {
        if (!nvim_host && host.debug_state().name == "nvim")
        {
            nvim_host = &host;
            nvim_leaf = id;
        }
    });

    if (nvim_host)
    {
        nvim_host->dispatch_action(action);
        host_manager_.set_focused(nvim_leaf);
        request_frame();
        return true;
    }

    // No existing NvimHost — create a vertical split with one.
    LeafId new_leaf = host_manager_.split_focused(SplitDirection::Vertical, HostKind::Nvim, *this);
    if (new_leaf == kInvalidLeaf)
        return false;

    refresh_window_layout();
    request_frame();

    IHost* new_host = host_manager_.host_for(new_leaf);
    if (new_host)
        new_host->dispatch_action(action);

    return true;
}

void App::update_diagnostics_panel()
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    auto [pixel_w, pixel_h] = window_->size_pixels();
    const int logical_w = window_->width_logical();
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const PixelScale pixel_scale = PixelScale::from_window(pixel_w, logical_w);
    ui_panel_.set_window_metrics(pixel_w, pixel_h, cell_w, cell_h, renderer_.grid()->padding(), pixel_scale.value());
}

HostViewport App::viewport_from_descriptor(const PaneDescriptor& desc) const
{
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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

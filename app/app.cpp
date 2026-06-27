#include "app.h"

#ifdef __APPLE__
#include "macos_menu.h"
#endif
#include "chrome_host.h"
#include "gui_action_handler.h"
#include "host_manager.h"
#include "input_dispatcher.h"
#include "session_id.h"
#include "session_listing.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <draxul/atlas_upload.h>
#include <draxul/grid_host_base.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/pixel_scale.h>
#include <draxul/render_test_driver.h>
#include <draxul/sdl_window.h>
#include <imgui.h>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

// Compute the pixel size for ImGui fonts from actual font metrics.
//
// FreeType's cell_height (face->size->metrics.height) includes ascender, descender, AND the
// font's internal leading (line gap).  ImGui adds its own line spacing on top, so passing
// cell_height directly produces oversized text.  Instead we use (ascender + descender) which
// is the actual glyph extent without the line gap.  This replaces the previous empirical
// formula `cell_height * (point_size - 2) / point_size` whose magic constant didn't scale
// correctly across different fonts and sizes.
float imgui_font_size_from_metrics(const FontMetrics& metrics)
{
    return static_cast<float>(metrics.ascender + metrics.descender);
}

void normalize_render_target_window_size(IWindow& window, const AppOptions& options)
{
    if (options.render_target_pixel_width <= 0 || options.render_target_pixel_height <= 0)
        return;
    window.normalize_render_target_window_size(options.render_target_pixel_width,
        options.render_target_pixel_height);
}

bool text_service_config_changed(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.font_size != rhs.font_size
        || lhs.enable_ligatures != rhs.enable_ligatures
        || lhs.font_path != rhs.font_path
        || lhs.bold_font_path != rhs.bold_font_path
        || lhs.italic_font_path != rhs.italic_font_path
        || lhs.bold_italic_font_path != rhs.bold_italic_font_path
        || lhs.fallback_paths != rhs.fallback_paths;
}

void restore_text_service_config(AppConfig& target, const AppConfig& source)
{
    target.font_size = source.font_size;
    target.enable_ligatures = source.enable_ligatures;
    target.font_path = source.font_path;
    target.bold_font_path = source.bold_font_path;
    target.italic_font_path = source.italic_font_path;
    target.bold_italic_font_path = source.bold_italic_font_path;
    target.fallback_paths = source.fallback_paths;
}

class CallbackInputRouter final : public IInputRouter
{
public:
    std::function<IHost*()> overlay_host_fn;
    std::function<HostManager*()> host_manager_fn;
    std::function<int(int, int)> hit_test_tab_fn;
    std::function<LeafId(int, int)> hit_test_pane_pill_fn;
    std::function<int()> tab_bar_height_phys_fn;
    std::function<std::pair<int, int>()> cell_size_phys_fn;
    std::function<void(int)> activate_tab_fn;
    std::function<void(int)> activate_pane_fn;
    std::function<void(int)> begin_tab_rename_fn;
    std::function<void(LeafId)> begin_pane_rename_fn;
    std::function<bool()> is_editing_fn;
    std::function<bool(const std::string&)> rename_text_input_fn;
    std::function<bool(int)> rename_key_fn;
    std::function<void()> commit_rename_fn;

    IHost* overlay_host() override
    {
        return overlay_host_fn ? overlay_host_fn() : nullptr;
    }

    HostManager* host_manager() override
    {
        return host_manager_fn ? host_manager_fn() : nullptr;
    }

    int hit_test_tab(int phys_x, int phys_y) override
    {
        return hit_test_tab_fn ? hit_test_tab_fn(phys_x, phys_y) : 0;
    }

    LeafId hit_test_pane_pill(int phys_x, int phys_y) override
    {
        return hit_test_pane_pill_fn ? hit_test_pane_pill_fn(phys_x, phys_y) : kInvalidLeaf;
    }

    int tab_bar_height_phys() override
    {
        return tab_bar_height_phys_fn ? tab_bar_height_phys_fn() : 0;
    }

    std::pair<int, int> cell_size_phys() override
    {
        return cell_size_phys_fn ? cell_size_phys_fn() : std::pair<int, int>{ 0, 0 };
    }

    void activate_tab(int one_based_index) override
    {
        if (activate_tab_fn)
            activate_tab_fn(one_based_index);
    }

    void activate_pane(int one_based_index) override
    {
        if (activate_pane_fn)
            activate_pane_fn(one_based_index);
    }

    void begin_tab_rename(int one_based_index) override
    {
        if (begin_tab_rename_fn)
            begin_tab_rename_fn(one_based_index);
    }

    void begin_pane_rename(LeafId leaf) override
    {
        if (begin_pane_rename_fn)
            begin_pane_rename_fn(leaf);
    }

    bool is_editing() override
    {
        return is_editing_fn && is_editing_fn();
    }

    bool rename_text_input(const std::string& text) override
    {
        return rename_text_input_fn && rename_text_input_fn(text);
    }

    bool rename_key(int keycode) override
    {
        return rename_key_fn && rename_key_fn(keycode);
    }

    void commit_rename() override
    {
        if (commit_rename_fn)
            commit_rename_fn();
    }
};

HostReloadConfig host_reload_config_from_app_config(const AppConfig& config)
{
    HostReloadConfig reload;
    reload.enable_ligatures = config.enable_ligatures;
    reload.terminal_fg = config.terminal.fg.empty()
        ? std::nullopt
        : parse_hex_color(config.terminal.fg);
    reload.terminal_bg = config.terminal.bg.empty()
        ? std::nullopt
        : parse_hex_color(config.terminal.bg);
    reload.font_size = config.font_size;
    reload.smooth_scroll = config.smooth_scroll;
    reload.scroll_speed = config.scroll_speed;
    reload.palette_bg_alpha = config.palette_bg_alpha;
    reload.markdown_font_size = config.markdown.font_size;
    reload.markdown_margin_columns = config.markdown.margin_columns;
    reload.selection_max_cells = config.terminal.selection_max_cells;
    reload.copy_on_select = config.terminal.copy_on_select;
    reload.paste_confirm_lines = config.terminal.paste_confirm_lines;
    reload.url_detection = config.terminal.url_detection;
    reload.enable_osc8_hyperlinks = config.terminal.enable_osc8_hyperlinks;
    reload.enable_shell_integration_marks = config.terminal.enable_shell_integration_marks;
    reload.scrollback_lines = config.scrollback_lines;
    return reload;
}

int64_t unix_now_seconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string trim_session_name(std::string_view name)
{
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.remove_suffix(1);
    return std::string(name);
}

uint64_t current_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
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
    if (options_.enable_session_attach)
        options_.enable_session_restore = true;
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
        for (const auto& warning : config_.warnings)
            push_toast(0, warning);
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

    if (!time_step("Session Attach", [this]() { return initialize_session_attach(); }))
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
                const bool start_hidden = options_.start_hidden_window
#ifdef DRAXUL_ENABLE_RENDER_TESTS
                    || (options_.render_target_pixel_width > 0 && !options_.show_render_test_window)
#endif
                    ;
                sdl->set_hidden(start_hidden);
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
            renderer_options.wait_for_vblank = !options_.no_vblank;
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
            diagnostics_host_ = std::make_unique<DiagnosticsPanelHost>();
            HostContext diagnostics_ctx;
            diagnostics_ctx.window = window_.get();
            diagnostics_ctx.grid_renderer = renderer_.grid();
            diagnostics_ctx.text_service = &text_service_;
            if (!diagnostics_host_ || !diagnostics_host_->initialize(diagnostics_ctx, *this))
            {
                last_init_error_ = "Failed to initialize the diagnostics panel.";
                return false;
            }
            diagnostics_host_->set_imgui_font(text_service_.primary_font_path(),
                imgui_font_size_from_metrics(text_service_.metrics()));
            diagnostics_host_->set_visible(options_.show_diagnostics_on_startup);
            refresh_window_layout();
            if (renderer_.imgui())
                diagnostics_host_->attach_imgui_host(*renderer_.imgui());
            if (!renderer_.imgui() || !renderer_.imgui()->initialize_imgui_backend())
            {
                last_init_error_ = "Failed to initialize the renderer ImGui backend.";
                return false;
            }
            return true;
        }))
        return false;

    if (!time_step("Host", [this]() { return initialize_chrome_host(); }))
        return false;

    if (active_host_manager().host())
        diagnostics_collector_.amend_last_step_label("Host (" + active_host_manager().host()->debug_state().name + ")");

    diagnostics_collector_.set_startup_total_ms(Ms(Clock::now() - init_start).count());

    wire_gui_actions();

    // Create the command palette overlay host — drawn last by render_imgui_overlay().
    {
        CommandPaletteHost::Deps palette_host_deps;
        palette_host_deps.gui_action_handler = &gui_action_handler_;
        palette_host_deps.keybindings = &config_.keybindings;
        palette_host_deps.palette_bg_alpha = &config_.palette_bg_alpha;
        palette_host_ = std::make_unique<CommandPaletteHost>(std::move(palette_host_deps));

        HostContext palette_ctx;
        palette_ctx.grid_renderer = renderer_.grid();
        palette_ctx.text_service = &text_service_;
        palette_ctx.window = window_.get();
        auto [pw, ph] = window_->size_pixels();
        palette_ctx.initial_viewport.pixel_size = { pw, ph };
        palette_host_->initialize(palette_ctx, *this);
    }

    // Create the toast notification overlay host — drawn above the palette.
    {
        toast_host_ = std::make_unique<ToastHost>();

        HostContext toast_ctx;
        toast_ctx.grid_renderer = renderer_.grid();
        toast_ctx.text_service = &text_service_;
        toast_ctx.window = window_.get();
        auto [tw, th] = window_->size_pixels();
        toast_ctx.initial_viewport.pixel_size = { tw, th };
        toast_host_->initialize(toast_ctx, *this);

        // Replay any toasts that were buffered before the host existed
        // (config warnings, font warnings, early init failures, etc.).
        auto buffered = std::move(pending_init_toasts_);
        pending_init_toasts_.clear();
        for (auto& t : buffered)
            push_toast(t.level, t.message);
    }

#ifdef __APPLE__
    macos_menu_ = std::make_unique<MacOsMenu>(gui_action_handler_);
#endif

    wire_window_callbacks();

    // Snapshot the initial window size so the pump loop's size-change check
    // has a correct baseline (avoids a spurious on_resize on the first frame).
    std::tie(last_pixel_w_, last_pixel_h_) = window_->size_pixels();

    saw_frame_ = false;
    running_ = true;
    // Render one initial composite frame after init so hosts that only request
    // redraws on state changes do not start on a blank window.
    request_frame();
    rebuild_render_tree();
    if (options_.enable_session_restore)
    {
        mark_session_attached();
        persist_session_state();
        persist_session_runtime_metadata(options_.enable_session_attach);
        last_session_checkpoint_time_ = std::chrono::steady_clock::now();
    }

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

    for (auto& warning : text_service_.take_font_warnings())
        push_toast(1, warning);

    const auto& metrics = text_service_.metrics();
    renderer_.grid()->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_.grid()->set_ascender(metrics.ascender);
    refresh_window_layout();
    return true;
}

bool App::initialize_session_attach()
{
    PERF_MEASURE();
    if (!options_.enable_session_attach)
        return true;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Initializing session attach server for '%s' (pid=%llu)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()));

    std::string error;
    if (!start_session_attach_server(&error))
    {
        last_init_error_ = error.empty()
            ? "Failed to start the session attach server."
            : error;
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to initialize session attach server for '%s': %s",
            options_.session_id.c_str(),
            last_init_error_.c_str());
        return false;
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Session attach server initialized for '%s' (pid=%llu)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()));
    return true;
}

bool App::start_session_attach_server(std::string* error)
{
    PERF_MEASURE();
    return session_attach_server_.start(options_.session_id, [this](SessionAttachServer::Command command) {
            if (command == SessionAttachServer::Command::Activate)
                external_attach_requested_.store(true);
            else if (command == SessionAttachServer::Command::Detach)
                external_detach_requested_.store(true);
            else if (command == SessionAttachServer::Command::Shutdown)
                external_session_shutdown_requested_.store(true);
            wake_window(); }, [this]() { return live_session_info(); }, [this](std::string_view session_name) {
            std::lock_guard lock(external_session_rename_mutex_);
            external_session_rename_requested_ = std::string(session_name);
            wake_window(); }, error);
}

void App::apply_font_metrics()
{
    PERF_MEASURE();
    const auto& metrics = text_service_.metrics();
    renderer_.grid()->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_.grid()->set_ascender(metrics.ascender);
    const float imgui_font_size = imgui_font_size_from_metrics(metrics);
    diagnostics_host_->set_imgui_font(text_service_.primary_font_path(), imgui_font_size);
    for (auto& ws : workspaces_)
    {
        ws->host_manager.for_each_host([this, imgui_font_size](LeafId, IHost& host) {
            host.set_imgui_font(text_service_.primary_font_path(), imgui_font_size);
            host.on_font_metrics_changed();
        });
    }
    refresh_window_layout();
    {
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(
            0, tab_y, window_->width_pixels(), diagnostics_host_->layout().terminal_height - tab_y);
    }
    request_frame();
}

Result<void, Error> App::reload_config()
{
    PERF_MEASURE();
    if (!options_.load_user_config)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Ignoring reload_config because user config loading is disabled.");
        return Result<void, Error>::err(Error(
            ErrorKind::ConfigApplyFailed,
            "User config loading is disabled; reload ignored."));
    }

    const AppConfig previous_config = config_;
    AppConfig reloaded_config = AppConfig::load();
    apply_overrides(reloaded_config, options_.config_overrides);
    config_document_ = ConfigDocument::load();
    config_ = std::move(reloaded_config);

    const bool text_config_needs_reload = text_service_config_changed(previous_config, config_);
    const bool scroll_config_changed = previous_config.smooth_scroll != config_.smooth_scroll
        || previous_config.scroll_speed != config_.scroll_speed;

    // Track a structured error if font reinit fails. We still fall back to
    // the previous font, but the caller can now see that the reload was only
    // partially successful.
    Result<void, Error> font_result = Result<void, Error>::ok();

    if (text_config_needs_reload)
    {
        if (const TextServiceConfig text_config = make_text_service_config();
            text_service_.initialize(text_config, config_.font_size, display_ppi_))
        {
            if (renderer_.imgui())
                renderer_.imgui()->rebuild_imgui_font_texture();
            apply_font_metrics();
        }
        else
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to apply reloaded font settings from %s; keeping the current font configuration.",
                ConfigDocument::default_path().string().c_str());
            font_result = Result<void, Error>::err(Error::config_apply(
                "Failed to apply reloaded font settings; reverted to previous font."));
            restore_text_service_config(config_, previous_config);
            const TextServiceConfig restored_text_config = make_text_service_config();
            if (!text_service_.initialize(restored_text_config, config_.font_size, display_ppi_))
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Failed to restore the previous font configuration after reload_config.");
                font_result = Result<void, Error>::err(Error::config_apply(
                    "Failed to restore previous font configuration after reload."));
            }
            else
            {
                if (renderer_.imgui())
                    renderer_.imgui()->rebuild_imgui_font_texture();
                apply_font_metrics();
            }
        }
    }

    if (scroll_config_changed)
        input_dispatcher_.set_scroll_config(config_.smooth_scroll, config_.scroll_speed);
    input_dispatcher_.set_chord_indicator_fade_ms(config_.chord_indicator_fade_ms);

    const HostReloadConfig host_reload = host_reload_config_from_app_config(config_);
    for (auto& ws : workspaces_)
    {
        ws->host_manager.for_each_host([&host_reload](LeafId, IHost& host) {
            host.on_config_reloaded(host_reload);
        });
    }

    request_frame();
    DRAXUL_LOG_INFO(LogCategory::App, "Reloaded config from %s",
        ConfigDocument::default_path().string().c_str());
    return font_result;
}

bool App::initialize_chrome_host()
{
    PERF_MEASURE();
    host_owner_lifetime_ = std::make_shared<int>(0);
    refresh_system_resource_snapshot(std::chrono::steady_clock::now());

    ChromeHost::Deps chrome_deps;
    chrome_deps.options = &options_;
    chrome_deps.config = &config_;
    chrome_deps.config_document = &config_document_;
    chrome_deps.window = window_.get();
    chrome_deps.grid_renderer = renderer_.grid();
    chrome_deps.imgui_host = renderer_.imgui();
    chrome_deps.text_service = &text_service_;
    chrome_deps.display_ppi = &display_ppi_;
    chrome_deps.owner_lifetime = host_owner_lifetime_;
    chrome_deps.compute_viewport = [this](const PaneDescriptor& desc) {
        return viewport_from_descriptor(desc);
    };
    chrome_deps.workspaces = &workspaces_;
    chrome_deps.active_workspace_id = &active_workspace_;
    chrome_deps.system_resource_snapshot = &system_resource_snapshot_;
    chrome_deps.weather_emoji = [this]() -> std::string {
        return weather_service_.emoji();
    };
    chrome_deps.weather_temperature = [this]() -> std::string {
        return weather_service_.temperature();
    };
    chrome_deps.chord_indicator = [this]() -> std::optional<std::pair<std::string, float>> {
        const auto state = input_dispatcher_.chord_indicator_state(std::chrono::steady_clock::now());
        if (!state.visible())
            return std::nullopt;
        return std::make_pair(state.text, state.alpha);
    };
    chrome_deps.set_workspace_name = [this](int workspace_id, std::string name) {
        for (auto& ws : workspaces_)
        {
            if (ws->id == workspace_id)
            {
                ws->name = std::move(name);
                ws->name_user_set = true;
                break;
            }
        }
        request_frame();
    };
    chrome_deps.set_pane_name = [this](LeafId leaf, std::string name) {
        // Apply to whichever workspace currently owns the leaf — pane edits
        // are always against the active workspace.
        active_host_manager().set_pane_name(leaf, std::move(name));
        request_frame();
    };
    chrome_deps.get_pane_name = [this](LeafId leaf) {
        return active_host_manager().pane_name(leaf);
    };
    chrome_deps.request_frame = [this]() { request_frame(); };
    chrome_host_ = std::make_unique<ChromeHost>(std::move(chrome_deps));

    if (!config_.weather_location.empty())
        weather_service_.start(config_.weather_location);

    {
        HostContext chrome_ctx{};
        chrome_ctx.window = window_.get();
        chrome_ctx.initial_viewport.pixel_size = { last_pixel_w_, last_pixel_h_ };
        chrome_host_->initialize(chrome_ctx, *this);
    }

    // Ensure ChromeHost has the actual window size (initial_viewport may be 0,0
    // if on_resize hasn't fired yet).
    {
        HostViewport vp;
        vp.pixel_size = { window_->width_pixels(), window_->height_pixels() };
        chrome_host_->set_viewport(vp);
    }

    session_name_ = options_.session_name.empty() ? options_.session_id : options_.session_name;

    bool restored_session = false;
    if (options_.enable_session_restore)
    {
        std::string metadata_error;
        if (auto metadata = load_session_runtime_metadata(options_.session_id, &metadata_error))
        {
            if (options_.session_name.empty() && !metadata->session_name.empty())
                session_name_ = metadata->session_name;
            session_last_attached_unix_s_ = metadata->last_attached_unix_s;
            session_last_detached_unix_s_ = metadata->last_detached_unix_s;
        }
        else if (!metadata_error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to load session metadata: %s", metadata_error.c_str());
        }

        if (pending_window_activation_ && window_)
        {
            // Bring the app window forward before we respawn restored panes so
            // any transient shell startup windows stay behind Draxul instead of
            // photobombing the restore.
            window_->activate();
            pending_window_activation_ = false;
        }

        // Skip session restore when the user explicitly specified --host on the
        // command line. The explicit host kind should win over a saved session
        // that may contain a different host type (e.g. --host megacity should
        // not restore a saved shell session).
        if (!options_.host_kind_explicit)
        {
            std::string session_error;
            if (auto saved_session = load_session_state(options_.session_id, &session_error);
                saved_session && !saved_session->workspaces.empty())
            {
                if (options_.session_name.empty() && !saved_session->session_name.empty())
                    session_name_ = saved_session->session_name;
                restored_session = restore_session_state(
                    window_->width_pixels(), diagnostics_host_->layout().terminal_height, *saved_session);
                if (!restored_session)
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Failed to restore saved shell session state; starting a fresh session.");
                }
            }
            else if (!session_error.empty())
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "Failed to load saved shell session state: %s", session_error.c_str());
            }
        }
    }

    if (!restored_session)
    {
        if (pending_window_activation_ && window_)
        {
            // Match the restore path: get Draxul in front before spawning the
            // first shell host so any transient console windows stay behind it.
            window_->activate();
            pending_window_activation_ = false;
        }
    }

    if (!restored_session
        && !create_initial_workspace(window_->width_pixels(), diagnostics_host_->layout().terminal_height))
    {
        // Clean up stale session state that may have contributed to the failure,
        // but don't retry — if the host can't be created (e.g. nvim not on PATH),
        // retrying won't help and makes CI smoke tests fail harder.
        delete_session_state(options_.session_id);
        delete_session_runtime_metadata(options_.session_id);
        return false;
    }

    {
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(
            0, tab_y, window_->width_pixels(), diagnostics_host_->layout().terminal_height - tab_y);
    }

    const float font_size = imgui_font_size_from_metrics(text_service_.metrics());
    active_host_manager().host()->set_imgui_font(text_service_.primary_font_path(), font_size);

    const std::string session_label = session_name_.empty() ? options_.session_id : session_name_;
    if (restored_session)
        push_toast(0, "Restored saved session '" + session_label + "'.");
    else if (options_.new_session_requested)
        push_toast(0, "Started new session '" + session_label + "'.");

    request_frame();
    return true;
}

void App::wire_gui_actions()
{
    PERF_MEASURE();
    GuiActionHandler::Deps gui_deps;
    gui_deps.text_service = &text_service_;
    gui_deps.ui_panel = diagnostics_host_ ? &diagnostics_host_->panel() : nullptr;
    gui_deps.focused_host = [this]() { return active_host_manager().focused_host(); };
    gui_deps.imgui_host = renderer_.imgui();
    gui_deps.config = &config_;
    gui_deps.on_font_changed = [this]() { apply_font_metrics(); };
    gui_deps.on_open_file_dialog = [this]() { window_->show_open_file_dialog(); };
    gui_deps.on_split_vertical = [this](std::optional<HostKind> kind) {
        LeafId new_leaf = kind
            ? active_host_manager().split_focused(SplitDirection::Vertical, *kind, *this)
            : active_host_manager().split_focused(SplitDirection::Vertical, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
        else
        {
            const std::string& err = active_host_manager().error();
            push_toast(2, err.empty() ? std::string("Failed to spawn split pane") : err);
        }
    };
    gui_deps.on_split_horizontal = [this](std::optional<HostKind> kind) {
        LeafId new_leaf = kind
            ? active_host_manager().split_focused(SplitDirection::Horizontal, *kind, *this)
            : active_host_manager().split_focused(SplitDirection::Horizontal, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
        else
        {
            const std::string& err = active_host_manager().error();
            push_toast(2, err.empty() ? std::string("Failed to spawn split pane") : err);
        }
    };
    gui_deps.on_panel_toggled = [this]() {
        refresh_window_layout();
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(
            0, tab_y, window_->width_pixels(), diagnostics_host_->layout().terminal_height - tab_y);
        update_diagnostics_panel();
        request_frame();
    };
    gui_deps.on_command_palette = [this]() {
        if (palette_host_)
            palette_host_->dispatch_action("toggle");
    };
    gui_deps.on_save_session_as = [this]() {
        open_save_session_prompt();
    };
    gui_deps.on_load_session = [this]() {
        open_load_session_picker();
    };
    gui_deps.on_edit_config = [this]() {
        HostLaunchOptions launch;
        launch.kind = HostKind::Nvim;
        launch.args = { ConfigDocument::default_path().string() };
        LeafId new_leaf = active_host_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
        else
        {
            const std::string& err = active_host_manager().error();
            push_toast(2, err.empty() ? std::string("Failed to open config in split pane") : err);
        }
    };
    gui_deps.on_reload_config = [this]() {
        // WI 24: reload_config() now returns a Result — surface non-OK outcomes
        // to the user via a toast instead of silently swallowing them.
        if (auto r = reload_config(); !r)
            push_toast(2, r.error().message);
    };
    gui_deps.on_toggle_zoom = [this]() {
        const int tab_y = chrome_host_->tab_bar_height();
        active_host_manager().toggle_zoom(
            window_->width_pixels(), diagnostics_host_->layout().terminal_height - tab_y);
        input_dispatcher_.set_host(detached_ ? nullptr : active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_close_pane = [this]() {
        if (active_host_manager().host_count() <= 1)
        {
            if (workspace_count() <= 1)
            {
                // Last pane in last workspace — Ghostty-style: stay
                // alive with no window so the Dock/tray icon can reopen.
                session_killed_ = true;
                delete_session_state(options_.session_id);
                delete_session_runtime_metadata(options_.session_id);
                input_dispatcher_.set_host(nullptr);
                for (auto& ws : workspaces_)
                    ws->host_manager.shutdown();
                workspaces_.clear();
                if (options_.enable_session_attach)
                    window_->hide();
                else
                    running_ = false;
                return;
            }
            // Last pane in this workspace — close the workspace, switch to another.
            input_dispatcher_.set_host(nullptr);
            int closing = active_workspace_id();
            close_workspace(closing);
            const int pw = window_->width_pixels();
            const int th = diagnostics_host_->layout().terminal_height;
            const int tab_y = chrome_host_->tab_bar_height();
            recompute_all_viewports(0, tab_y, pw, th - tab_y);
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
            return;
        }
        input_dispatcher_.set_host(nullptr);
        active_host_manager().close_focused();
        input_dispatcher_.set_host(detached_ ? nullptr : active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_restart_host = [this]() {
        input_dispatcher_.set_host(nullptr);
        if (active_host_manager().restart_focused(*this))
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
        else
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
        }
    };
    gui_deps.on_swap_pane = [this]() {
        if (active_host_manager().swap_focused_with_next())
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
    };
    auto focus_pane = [this](FocusDirection dir) {
        if (active_host_manager().focus_direction(dir))
        {
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
    };
    gui_deps.on_focus_left = [focus_pane]() { focus_pane(FocusDirection::Left); };
    gui_deps.on_focus_right = [focus_pane]() { focus_pane(FocusDirection::Right); };
    gui_deps.on_focus_up = [focus_pane]() { focus_pane(FocusDirection::Up); };
    gui_deps.on_focus_down = [focus_pane]() { focus_pane(FocusDirection::Down); };
    auto resize_pane = [this](FocusDirection dir) {
        HostManager& hm = active_host_manager();
        DividerId id = hm.find_focused_ancestor_divider(dir);
        if (id == kInvalidDivider)
            return;
        // Positive delta grows the first child (the leaf on the left/top side
        // of the divider). For Left/Up actions we shrink the first child;
        // for Right/Down we grow it.
        const float step = 0.05f;
        const float delta
            = (dir == FocusDirection::Right || dir == FocusDirection::Down) ? step : -step;
        const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
        hm.nudge_divider(id, delta, cw, ch);
        request_frame();
    };
    gui_deps.on_resize_pane_left = [resize_pane]() { resize_pane(FocusDirection::Left); };
    gui_deps.on_resize_pane_right = [resize_pane]() { resize_pane(FocusDirection::Right); };
    gui_deps.on_resize_pane_up = [resize_pane]() { resize_pane(FocusDirection::Up); };
    gui_deps.on_resize_pane_down = [resize_pane]() { resize_pane(FocusDirection::Down); };
    gui_deps.on_new_tab = [this](std::optional<HostKind> kind) {
        const int pw = window_->width_pixels();
        const int th = diagnostics_host_->layout().terminal_height;
        int id = add_workspace(pw, th, kind);
        if (id >= 0)
        {
            // Set the font on the new host so ImGui uses the app's font, not the default.
            if (IHost* h = active_host_manager().host())
            {
                const float font_size = imgui_font_size_from_metrics(text_service_.metrics());
                h->set_imgui_font(text_service_.primary_font_path(), font_size);
            }
            // Recompute ALL workspace viewports with the (possibly new) tab bar offset.
            const int tab_y = chrome_host_->tab_bar_height();
            recompute_all_viewports(0, tab_y, pw, th - tab_y);
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
    };
    gui_deps.on_close_tab = [this]() {
        if (workspace_count() <= 1)
            return;
        int closing = active_workspace_id();
        input_dispatcher_.set_host(nullptr);
        close_workspace(closing);
        // Recompute all viewports with the (possibly changed) tab bar offset.
        const int pw = window_->width_pixels();
        const int th = diagnostics_host_->layout().terminal_height;
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(0, tab_y, pw, th - tab_y);
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_next_tab = [this]() {
        next_workspace();
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_prev_tab = [this]() {
        prev_workspace();
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_activate_tab = [this](int index) {
        activate_workspace_by_index(index);
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    gui_deps.on_rename_tab = [this]() {
        if (chrome_host_)
            chrome_host_->begin_tab_rename_by_id(active_workspace_);
        request_frame();
    };
    gui_deps.on_move_tab_left = [this]() {
        move_workspace(-1);
        request_frame();
    };
    gui_deps.on_move_tab_right = [this]() {
        move_workspace(1);
        request_frame();
    };
    gui_deps.on_duplicate_pane = [this]() {
        if (auto* host = active_host_manager().focused_host())
        {
            const std::string cwd = host->current_working_directory();
            HostLaunchOptions launch;
            launch.kind = HostManager::platform_default_split_host_kind();
            launch.enable_ligatures = config_.enable_ligatures;
            if (!cwd.empty())
                launch.working_dir = cwd;
            active_host_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
    };
    gui_deps.on_equalize_panes = [this]() {
        active_host_manager().equalize_splits(*this);
        request_frame();
    };
    gui_deps.on_rename_pane = [this]() {
        if (!chrome_host_)
            return;
        const LeafId leaf = active_host_manager().focused_leaf();
        if (leaf == kInvalidLeaf)
            return;
        chrome_host_->begin_pane_rename(leaf);
        request_frame();
    };
    gui_deps.broadcast_action = [this](std::string_view action) {
        active_host_manager().for_each_host(
            [action](LeafId, IHost& h) { h.dispatch_action(action); });
        request_frame();
    };
    gui_deps.on_test_toast = [this]() {
        // Cycle through info / warn / error so all three styles can be exercised
        // from a single command. The choice of message is intentionally light:
        // a hint that explains exactly what the test command does.
        static int counter = 0;
        const int level = counter % 3;
        ++counter;
        const char* msg = nullptr;
        switch (level)
        {
        case 0:
            msg = "Toast test: this is an info notification";
            break;
        case 1:
            msg = "Toast test: this is a warning notification";
            break;
        default:
            msg = "Toast test: this is an error notification";
            break;
        }
        push_toast(level, msg);
    };
    gui_action_handler_ = GuiActionHandler(std::move(gui_deps));

    // CommandPalette deps are now wired inside CommandPaletteHost::initialize().
}

void App::open_save_session_prompt()
{
    if (!palette_host_)
        return;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = !session_name_.empty()
        ? session_name_
        : (!options_.session_name.empty() ? options_.session_name : options_.session_id);
    request.on_submit = [this](std::string name) {
        auto saved = save_session_as(name);
        if (!saved)
        {
            push_toast(2, saved.error().message);
            return;
        }
        push_toast(0, "Saved session '" + name + "'.");
    };

    if (!palette_host_->open_prompt(std::move(request)))
        push_toast(2, "Unable to open session name prompt.");
}

void App::open_load_session_picker()
{
    if (!palette_host_)
        return;

    std::string list_error;
    auto sessions = list_known_sessions(&list_error);
    if (!list_error.empty())
    {
        push_toast(2, list_error);
        return;
    }
    if (sessions.empty())
    {
        push_toast(0, "No saved sessions found.");
        return;
    }

    CommandPalette::ChoiceRequest request;
    request.title = "Load Session";
    request.entries.reserve(sessions.size());
    for (const auto& session : sessions)
    {
        const std::string name = session_entry_name(session);
        const std::string hint = session_entry_hint(session);
        request.entries.push_back({
            .id = session.session_id,
            .name = name,
            .shortcut_hint = hint,
            .search_text = name + " " + session.session_id + " " + hint,
        });
    }
    request.on_submit = [this](std::string session_id) {
        auto loaded = load_session(session_id);
        if (!loaded)
        {
            push_toast(2, loaded.error().message);
            return;
        }
        push_toast(0, "Loaded session '" + session_id + "'.");
    };

    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open session picker.");
}

void App::wire_window_callbacks()
{
    PERF_MEASURE();
    InputDispatcher::Deps disp_deps;
    disp_deps.keybindings = &config_.keybindings;
    disp_deps.gui_action_handler = &gui_action_handler_;
    disp_deps.window = window_.get();
    auto router = std::make_unique<CallbackInputRouter>();
    router->overlay_host_fn = [this]() -> IHost* {
        return (palette_host_ && palette_host_->is_active()) ? palette_host_.get() : nullptr;
    };
    disp_deps.ui_panel = diagnostics_host_ ? &diagnostics_host_->panel() : nullptr;
    disp_deps.host = active_host_manager().host();
    router->host_manager_fn = [this]() -> HostManager* { return &active_host_manager(); };
    disp_deps.smooth_scroll = config_.smooth_scroll;
    disp_deps.scroll_speed = config_.scroll_speed;
    disp_deps.pixel_scale = PixelScale::from_window(window_->width_pixels(), window_->width_logical());
    disp_deps.request_frame = [this]() { request_frame(); };
    disp_deps.on_resize = [this](int w, int h) { on_resize(w, h); };
    disp_deps.on_display_scale_changed = [this](float ppi) { on_display_scale_changed(ppi); };
    router->hit_test_tab_fn = [this](int px, int py) { return chrome_host_ ? chrome_host_->hit_test_tab(px, py) : 0; };
    router->tab_bar_height_phys_fn = [this]() { return chrome_host_ ? chrome_host_->tab_bar_height() : 0; };
    router->cell_size_phys_fn = [this]() {
        return renderer_.grid() ? renderer_.grid()->cell_size_pixels() : std::pair<int, int>{ 0, 0 };
    };
    router->activate_tab_fn = [this](int index) {
        activate_workspace_by_index(index);
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    router->activate_pane_fn = [this](int index) {
        activate_pane_by_index(index);
        input_dispatcher_.set_host(active_host_manager().focused_host());
        request_frame();
    };
    router->begin_tab_rename_fn = [this](int tab_index) {
        // Activate the tab before editing so the user always edits the
        // visually-active workspace.
        activate_workspace_by_index(tab_index);
        input_dispatcher_.set_host(active_host_manager().focused_host());
        chrome_host_->begin_tab_rename(tab_index);
        request_frame();
    };
    // Reports any active rename session (tab OR pane) so the dispatcher's
    // click-outside-commit and key-routing logic apply uniformly to both.
    router->is_editing_fn = [this]() { return chrome_host_ && chrome_host_->is_editing(); };
    router->hit_test_pane_pill_fn = [this](int px, int py) -> LeafId {
        return chrome_host_ ? chrome_host_->hit_test_pane_status_pill(px, py) : kInvalidLeaf;
    };
    router->begin_pane_rename_fn = [this](LeafId leaf) {
        if (chrome_host_)
            chrome_host_->begin_pane_rename(leaf);
        request_frame();
    };
    router->rename_text_input_fn = [this](const std::string& text) {
        return chrome_host_ && chrome_host_->on_rename_text_input(text);
    };
    router->rename_key_fn = [this](int keycode) {
        return chrome_host_ && chrome_host_->on_rename_key(keycode);
    };
    router->commit_rename_fn = [this]() {
        if (chrome_host_)
            chrome_host_->commit_tab_rename();
    };
    input_router_ = std::move(router);
    disp_deps.router = input_router_.get();
    input_dispatcher_ = InputDispatcher(std::move(disp_deps));
    input_dispatcher_.set_chord_indicator_fade_ms(config_.chord_indicator_fade_ms);
    input_dispatcher_.connect(*window_);
    window_->on_close_requested = [this]() { on_window_close_requested(); };
    window_->on_quit_requested = [this]() { request_quit(); };
    window_->on_dock_reopen = [this]() {
        // Fires when the user clicks the Dock icon with no visible windows in
        // the opt-in persistent-app mode.
        if (options_.enable_session_attach)
            external_attach_requested_ = true;
    };
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
        if (active_host_manager().host() && active_host_manager().host()->runtime_state().content_ready && saw_frame_)
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
    last_render_test_error_.clear();

    RenderTestDriverEnv env;
    env.pump_once = [this](std::optional<std::chrono::steady_clock::time_point> wait_deadline) {
        return pump_once(wait_deadline);
    };
    env.request_frame = [this]() { request_frame(); };
    env.is_running = [this]() { return running_; };
    env.saw_frame = [this]() { return saw_frame_; };
    env.frame_requested = [this]() { return frame_requested_; };
    env.active_host_state = [this]() -> std::optional<HostRuntimeState> {
        if (auto* host = active_host_manager().host())
            return host->runtime_state();
        return std::nullopt;
    };
    env.capture = [this]() { return renderer_.capture(); };
    env.enable_diagnostics_panel = [this]() {
        diagnostics_host_->set_visible(true);
        refresh_window_layout();
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(0, tab_y, window_->width_pixels(), diagnostics_host_->layout().terminal_height - tab_y);
        update_diagnostics_panel();
        request_frame();
    };
    env.diagnostics_panel_render_time = [this]() -> std::optional<std::chrono::steady_clock::time_point> {
        if (!diagnostics_host_)
            return std::nullopt;
        return diagnostics_host_->last_render_time();
    };

    RenderTestDriverOptions driver_options;
    driver_options.timeout = timeout;
    driver_options.settle = settle;
    driver_options.want_diagnostics = options_.show_diagnostics_in_render_test;
    auto result = run_render_test_driver(env, driver_options);
    last_render_test_error_ = std::move(result.error);
    return std::move(result.frame);
}

bool App::close_dead_panes()
{
    PERF_MEASURE();
    std::vector<LeafId> dead;
    active_host_manager().for_each_host([this, &dead](LeafId id, const IHost& h) {
        const std::string pane_id = active_host_manager().pane_id(id);
        if (h.is_running())
        {
            if (!pane_id.empty())
                announced_dead_panes_.erase(pane_id);
            return;
        }
        if (active_host_manager().should_preserve_dead_leaf(id))
        {
            if (!pane_id.empty() && announced_dead_panes_.insert(pane_id).second)
            {
                const std::string pane_label = active_host_manager().pane_name(id).empty()
                    ? pane_id
                    : active_host_manager().pane_name(id);
                push_toast(1, "Pane '" + pane_label + "' exited unexpectedly. Use restart_host to respawn it.");
            }
            return;
        }
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
        announced_dead_panes_.erase(active_host_manager().pane_id(id));
        if (active_host_manager().host_count() == 1)
        {
            // Last pane in this workspace died.
            if (workspace_count() <= 1)
            {
                // Last pane in the last workspace. Persistent-app mode keeps a
                // hidden owner around; normal mode exits after clearing state.
                session_killed_ = true;
                delete_session_state(options_.session_id);
                delete_session_runtime_metadata(options_.session_id);
                input_dispatcher_.set_host(nullptr);
                // Tear down all dead workspaces so reattach_window() starts clean.
                for (auto& ws : workspaces_)
                    ws->host_manager.shutdown();
                workspaces_.clear();
                if (options_.enable_session_attach)
                    window_->hide();
                else
                    running_ = false;
                return false;
            }
            // Close this workspace and switch to another.
            int closing = active_workspace_id();
            close_workspace(closing);
            const int pw = window_->width_pixels();
            const int th = diagnostics_host_->layout().terminal_height;
            const int tab_y = chrome_host_->tab_bar_height();
            recompute_all_viewports(0, tab_y, pw, th - tab_y);
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
            return active_host_manager().host() != nullptr;
        }
        active_host_manager().close_leaf(id);
    }
    return active_host_manager().host() != nullptr;
}

void App::rebuild_render_tree()
{
    render_root_ = RenderNode{};
    render_root_.tag = "root";

    const auto& hm = active_host_manager();
    const bool zoomed = hm.is_zoomed();

    // Chrome host draws pane dividers / tab bar — hidden when zoomed.
    if (chrome_host_)
        render_root_.children.push_back({ chrome_host_.get(), !zoomed, "chrome", {} });

    // Active workspace's hosts.
    RenderNode ws_node{ nullptr, true, "workspace", {} };
    hm.for_each_host([&ws_node, zoomed, &hm](LeafId id, IHost& h) {
        const bool vis = !zoomed || id == hm.zoomed_leaf();
        ws_node.children.push_back({ &h, vis, "host", {} });
    });
    render_root_.children.push_back(std::move(ws_node));

    // Diagnostics overlay.
    if (diagnostics_host_)
        render_root_.children.push_back({ diagnostics_host_.get(), diagnostics_host_->visible(), "diagnostics", {} });

    // Command palette.
    if (palette_host_)
        render_root_.children.push_back({ palette_host_.get(), true, "palette", {} });

    // Toast notifications (topmost layer).
    if (toast_host_)
        render_root_.children.push_back({ toast_host_.get(), true, "toast", {} });
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
    if (auto* host = active_host_manager().focused_host())
        host->set_scroll_offset(input_dispatcher_.scroll_fraction() * static_cast<float>(ch));
    input_dispatcher_.clear_scroll_event();

    rebuild_render_tree();

    // Upload any dirty atlas regions once per frame, before begin_frame(),
    // so all subsystems share a single upload and the GPU texture is current
    // when rendering starts.
    upload_atlas_dirty_region(text_service_, *renderer_.grid(), atlas_upload_scratch_);

    const auto frame_start = std::chrono::steady_clock::now();
    IFrameContext* frame = renderer_.grid()->begin_frame();
    if (!frame)
    {
        runtime_perf_collector().cancel_frame();
        return false;
    }

    walk_draw(render_root_, *frame);

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
        if (external_attach_requested_.exchange(false))
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Processing external attach request for '%s' (pid=%llu, detached=%d)",
                options_.session_id.c_str(),
                static_cast<unsigned long long>(current_process_id()),
                detached_ ? 1 : 0);
            reattach_window();
        }
        if (external_detach_requested_.exchange(false))
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Processing external detach request for '%s' (pid=%llu)",
                options_.session_id.c_str(),
                static_cast<unsigned long long>(current_process_id()));
            detach_window();
        }
        {
            std::optional<std::string> renamed_session;
            {
                std::lock_guard lock(external_session_rename_mutex_);
                renamed_session.swap(external_session_rename_requested_);
            }
            if (renamed_session)
                rename_session(std::move(*renamed_session));
        }
        if (external_session_shutdown_requested_.exchange(false))
        {
            kill_session();
            running_ = false;
            return false;
        }

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
        apply_pending_resize();

        runtime_perf_collector().begin_frame();

        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(active_host_manager().focused_host());

        // Pump all visible hosts via tree walk.
        rebuild_render_tree();
        walk_pump(render_root_);
        refresh_workspace_default_names();
        const auto now = std::chrono::steady_clock::now();
        maybe_checkpoint_session(now);
        refresh_system_resource_snapshot(now);
        if (input_dispatcher_.update(now, config_.chord_timeout_ms))
            request_frame();

        // Re-check after pumping (hosts can die during pump).
        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(active_host_manager().focused_host());

        if (frame_requested_)
        {
            if (detached_)
                frame_requested_ = false;
            else
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

void App::refresh_system_resource_snapshot(std::chrono::steady_clock::time_point now)
{
    if (!system_resource_monitor_.refresh(now))
        return;

    system_resource_snapshot_ = system_resource_monitor_.snapshot();
    request_frame();
}

void App::on_resize(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    if (pixel_w == last_pixel_w_ && pixel_h == last_pixel_h_)
        return;
    pending_window_resize_ = std::pair{ pixel_w, pixel_h };
}

void App::apply_pending_resize()
{
    if (!pending_window_resize_)
        return;

    const auto [pixel_w, pixel_h] = *pending_window_resize_;
    pending_window_resize_.reset();
    if (pixel_w == last_pixel_w_ && pixel_h == last_pixel_h_)
        return;

    last_pixel_w_ = pixel_w;
    last_pixel_h_ = pixel_h;
    renderer_.grid()->resize(pixel_w, pixel_h);
    refresh_window_layout();
    {
        const int tab_y = chrome_host_->tab_bar_height();
        recompute_all_viewports(
            0, tab_y, pixel_w, diagnostics_host_->layout().terminal_height - tab_y);
    }
    if (chrome_host_)
    {
        HostViewport vp;
        vp.pixel_size = { pixel_w, pixel_h };
        chrome_host_->set_viewport(vp);
    }
    if (palette_host_)
    {
        HostViewport vp;
        vp.pixel_size = { pixel_w, pixel_h };
        palette_host_->set_viewport(vp);
    }
    if (toast_host_)
    {
        HostViewport vp;
        vp.pixel_size = { pixel_w, pixel_h };
        toast_host_->set_viewport(vp);
    }
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
    active_host_manager().for_each_host([](LeafId, IHost& h) {
        h.request_close();
    });
    running_ = false;
}

void App::on_window_close_requested()
{
    if (can_detach_window())
    {
        detach_window();
        return;
    }
    request_quit();
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
    // Find an existing NvimHost via the typed capability query. The first
    // host (in HostManager iteration order) reporting is_nvim_host()==true wins;
    // this is the same selection policy as before, just without the debug-string
    // heuristic.
    IHost* nvim_host = nullptr;
    LeafId nvim_leaf = kInvalidLeaf;
    active_host_manager().for_each_host([&nvim_host, &nvim_leaf](LeafId id, IHost& host) {
        if (!nvim_host && host.is_nvim_host())
        {
            nvim_host = &host;
            nvim_leaf = id;
        }
    });

    if (nvim_host)
    {
        nvim_host->dispatch_action(action);
        active_host_manager().set_focused(nvim_leaf);
        request_frame();
        return true;
    }

    // No existing NvimHost — create a vertical split with one.
    LeafId new_leaf = active_host_manager().split_focused(SplitDirection::Vertical, HostKind::Nvim, *this);
    if (new_leaf == kInvalidLeaf)
    {
        const std::string& err = active_host_manager().error();
        push_toast(2, err.empty() ? std::string("Failed to spawn nvim host") : err);
        return false;
    }

    refresh_window_layout();
    request_frame();

    IHost* new_host = active_host_manager().host_for(new_leaf);
    if (new_host)
        new_host->dispatch_action(action);

    return true;
}

bool App::open_markdown_source(std::string_view path)
{
    const LeafId caller_leaf = active_host_manager().focused_leaf();
    auto restore_caller_focus = [this, caller_leaf]() {
        if (caller_leaf != kInvalidLeaf && active_host_manager().host_for(caller_leaf))
            active_host_manager().set_focused(caller_leaf);
        input_dispatcher_.set_host(active_host_manager().focused_host());
    };

    IHost* markdown_host = nullptr;
    active_host_manager().for_each_host([&markdown_host](LeafId, IHost& host) {
        if (!markdown_host && host.is_markdown_host())
        {
            markdown_host = &host;
        }
    });

    if (markdown_host)
    {
        std::string action = "open_file:";
        action.append(path);
        if (!markdown_host->dispatch_action(action))
        {
            push_toast(2, "Failed to open Markdown source: " + std::string(path));
            return false;
        }

        restore_caller_focus();
        request_frame();
        return true;
    }

    HostLaunchOptions launch;
    launch.kind = HostKind::Markdown;
    launch.source_path = std::string(path);
    LeafId new_leaf = active_host_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
    if (new_leaf == kInvalidLeaf)
    {
        const std::string& err = active_host_manager().error();
        push_toast(2, err.empty() ? std::string("Failed to spawn markdown host") : err);
        return false;
    }

    refresh_window_layout();
    restore_caller_focus();
    request_frame();
    return true;
}

void App::push_toast(int level, std::string_view message)
{
    if (!config_.enable_toast_notifications)
        return;

    if (!toast_host_)
    {
        pending_init_toasts_.push_back({ level, std::string(message) });
        return;
    }

    auto toast_level = gui::ToastLevel::Info;
    if (level == 1)
        toast_level = gui::ToastLevel::Warn;
    else if (level >= 2)
        toast_level = gui::ToastLevel::Error;
    toast_host_->push(toast_level, std::string(message), config_.toast_duration_s);

    // WI 12: wake the main loop so an idle app surfaces the toast immediately.
    // push_toast() may be called from background threads; wake_window() is
    // thread-safe (SDL_PushEvent is documented thread-safe), whereas
    // request_frame() writes a plain bool and must stay on the main thread.
    wake_window();
}

void App::update_diagnostics_panel()
{
    PERF_MEASURE();
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();

    DiagnosticPanelState panel;
    panel.visible = diagnostics_host_ && diagnostics_host_->visible();
    panel.display_ppi = display_ppi_;
    panel.cell_size = { cell_w, cell_h };
    panel.frame_ms = frame_timer_.last_ms();
    panel.average_frame_ms = frame_timer_.average_ms();
    panel.atlas_usage_ratio = text_service_.atlas_usage_ratio();
    panel.atlas_glyph_count = text_service_.atlas_glyph_count();
    panel.atlas_reset_count = text_service_.atlas_reset_count();
    panel.startup_steps = diagnostics_collector_.startup_steps();
    panel.startup_total_ms = diagnostics_collector_.startup_total_ms();

    if (active_host_manager().host())
    {
        const HostDebugState host_state = active_host_manager().host()->debug_state();
        panel.grid_size = { host_state.grid_cols, host_state.grid_rows };
        panel.dirty_cells = host_state.dirty_cells;
    }

    panel.host_panes.push_back({ "ChromeHost", { 0, 0 }, { last_pixel_w_, last_pixel_h_ } });

    auto& hm = active_host_manager();
    hm.for_each_host([&panel, &hm](LeafId id, IHost& h) {
        const auto dbg = h.debug_state();
        const auto pd = hm.tree().descriptor_for(id);
        panel.host_panes.push_back({ dbg.name, pd.pixel_pos, pd.pixel_size });
    });

    if (diagnostics_host_ && diagnostics_host_->visible())
    {
        const auto& dl = diagnostics_host_->layout();
        panel.host_panes.push_back({ "Diagnostics", { 0, dl.panel_y }, { dl.window_size.x, dl.panel_height } });
    }

    if (diagnostics_host_)
        diagnostics_host_->update_diagnostic_state(panel);
}

void App::refresh_window_layout()
{
    PERF_MEASURE();
    auto [pixel_w, pixel_h] = window_->size_pixels();
    const int logical_w = window_->width_logical();
    auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const PixelScale pixel_scale = PixelScale::from_window(pixel_w, logical_w);
    if (diagnostics_host_)
        diagnostics_host_->set_window_metrics(pixel_w, pixel_h, cell_w, cell_h, renderer_.grid()->padding(), pixel_scale.value());
}

HostViewport App::viewport_from_descriptor(const PaneDescriptor& desc) const
{
    PERF_MEASURE();
    const int padding = renderer_.grid()->padding();
    const auto [cell_w, cell_h] = renderer_.grid()->cell_size_pixels();
    const auto& layout = diagnostics_host_->layout();

    HostViewport viewport;
    viewport.pixel_pos = desc.pixel_pos;
    viewport.pixel_size = desc.pixel_size;
    viewport.padding = padding;
    viewport.pixel_scale = layout.pixel_scale;

    // WI 78: reserve a one-cell strip at the bottom of every pane for the
    // per-pane status bar. ChromeHost draws the strip in the reserved area.
    if (config_.show_pane_status && cell_h > 0)
    {
        const int reserved = std::min(viewport.pixel_size.y, cell_h);
        viewport.pixel_size.y -= reserved;
    }

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

    auto deadline = walk_deadline(render_root_);
    bool any_host_running = walk_any_running(render_root_);
    if (wait_deadline && (!deadline || *wait_deadline < *deadline))
        deadline = wait_deadline;

    if (!deadline)
    {
        if (any_host_running)
            return kHostPollIntervalMs;
        // When no hosts are running (e.g. window hidden after all panes
        // closed), use a short poll interval so we can respond to dock
        // reopen / tray icon clicks promptly.
        return window_ && !window_->is_visible() ? 200 : -1;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= *deadline)
        return 0;

    int ms = std::max(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count()));
    if (any_host_running)
        ms = std::min(ms, kHostPollIntervalMs);
    return ms;
}

// ---------------------------------------------------------------------------
// Workspace management (moved from ChromeHost)
// ---------------------------------------------------------------------------

HostManager::Deps App::make_host_manager_deps()
{
    HostManager::Deps deps;
    deps.options = &options_;
    deps.config = &config_;
    deps.config_document = &config_document_;
    deps.window = window_.get();
    deps.grid_renderer = renderer_.grid();
    deps.imgui_host = renderer_.imgui();
    deps.text_service = &text_service_;
    deps.display_ppi = &display_ppi_;
    deps.owner_lifetime = host_owner_lifetime_;
    deps.compute_viewport = [this](const PaneDescriptor& desc) {
        return viewport_from_descriptor(desc);
    };
    return deps;
}

void App::refresh_workspace_default_names()
{
    for (auto& ws : workspaces_)
    {
        if (ws->name_user_set)
            continue;
        IHost* focused = ws->host_manager.focused_host();
        if (!focused)
            continue;
        const std::string cwd = focused->current_working_directory();
        if (cwd.empty())
            continue;
        // Strip trailing slashes, then take the basename.
        std::string_view sv = cwd;
        while (sv.size() > 1 && sv.back() == '/')
            sv.remove_suffix(1);
        const auto last_slash = sv.rfind('/');
        const std::string_view basename
            = (last_slash != std::string_view::npos) ? sv.substr(last_slash + 1) : sv;
        if (basename.empty())
            continue;
        std::string new_name(basename);
        if (ws->name == new_name)
            continue;
        ws->name = std::move(new_name);
        request_frame();
    }
}

bool App::can_snapshot_session_state() const
{
    if (!options_.enable_session_restore || workspaces_.empty())
        return false;

    for (const auto& ws : workspaces_)
    {
        if (!ws->host_manager.has_detachable_shell_session())
            return false;
    }

    return true;
}

bool App::can_detach_window() const
{
    return options_.enable_session_attach && can_snapshot_session_state();
}

void App::detach_window()
{
    PERF_MEASURE();
    if (detached_ || !can_detach_window())
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Detaching window for session '%s' (pid=%llu)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()));

    detached_ = true;
    mark_session_detached();
    persist_session_state();
    persist_session_runtime_metadata(true);
    input_dispatcher_.set_host(nullptr);
    pending_window_activation_ = false;
    frame_requested_ = false;
    window_->hide();
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Window detached and hidden for session '%s' (pid=%llu)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()));
}

void App::reattach_window()
{
    PERF_MEASURE();
    const bool was_detached = detached_;
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Reattaching window for session '%s' (pid=%llu, was_detached=%d, visible=%d)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()),
        was_detached ? 1 : 0,
        window_ && window_->is_visible() ? 1 : 0);
    detached_ = false;
    mark_session_attached();
    pending_window_activation_ = true;
    input_dispatcher_.set_host(active_host_manager().focused_host());
    if (was_detached)
    {
        if (IHost* focused = active_host_manager().focused_host())
            focused->on_focus_gained();
        const std::string session_label = session_name_.empty() ? options_.session_id : session_name_;
        push_toast(0, "Reattached live session '" + session_label + "'.");
    }
    else if (window_ && !window_->is_visible())
    {
        window_->show();
    }

    // If the session was killed (all panes closed), create a fresh workspace
    // so the user has something to interact with.
    if (workspaces_.empty() || !active_host_manager().host())
    {
        session_killed_ = false;
        const int pw = window_->width_pixels();
        const int th = diagnostics_host_ ? diagnostics_host_->layout().terminal_height
                                         : window_->height_pixels();
        const int tab_y = chrome_host_ ? chrome_host_->tab_bar_height() : 0;
        add_workspace(pw, th - tab_y);
        recompute_all_viewports(0, tab_y, pw, th - tab_y);
        input_dispatcher_.set_host(active_host_manager().focused_host());
    }

    persist_session_runtime_metadata(true);
    request_frame();
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Reattach finished for session '%s' (pid=%llu, visible=%d)",
        options_.session_id.c_str(),
        static_cast<unsigned long long>(current_process_id()),
        window_ && window_->is_visible() ? 1 : 0);
}

void App::kill_session()
{
    PERF_MEASURE();
    if (session_killed_)
        return;

    session_killed_ = true;
    detached_ = false;
    pending_window_activation_ = false;
    input_dispatcher_.set_host(nullptr);

    std::string error;
    if (!delete_session_state(options_.session_id, &error) && !error.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to delete session state for %s: %s",
            options_.session_id.c_str(), error.c_str());
    }
    error.clear();
    if (!delete_session_runtime_metadata(options_.session_id, &error) && !error.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to delete session metadata for %s: %s",
            options_.session_id.c_str(), error.c_str());
    }

    active_host_manager().for_each_host([](LeafId, IHost& h) {
        h.request_close();
    });
    // Note: callers decide whether to set running_ = false (quit) or
    // hide the window (Ghostty-style stay-in-Dock). kill_session() only
    // cleans up session state; it does not stop the event loop.
}

void App::rename_session(std::string name)
{
    PERF_MEASURE();
    if (name.empty() || name == session_name_)
        return;

    session_name_ = std::move(name);
    persist_session_state();
    persist_session_runtime_metadata(options_.enable_session_attach);
    request_frame();
}

Result<void, Error> App::load_session(std::string_view raw_session_id)
{
    PERF_MEASURE();
    const std::string target_id = trim_session_name(raw_session_id);
    if (target_id.empty())
        return Result<void, Error>::err(Error::invalid_argument("Select a session to load."));
    if (!options_.enable_session_restore)
    {
        return Result<void, Error>::err(
            Error::invalid_argument("Session restore is not enabled for this launch."));
    }
    if (target_id == options_.session_id)
        return Result<void, Error>::ok();
    if (!can_snapshot_session_state())
    {
        return Result<void, Error>::err(
            Error::invalid_argument("Current panes cannot be saved before loading another session."));
    }

    std::string probe_error;
    const auto probe_status = SessionAttachServer::probe(target_id, &probe_error);
    if (probe_status == SessionAttachServer::ProbeStatus::Running)
    {
        return Result<void, Error>::err(Error::invalid_argument(
            "Session '" + target_id + "' is already running. Use the startup session picker to attach to it."));
    }
    if (probe_status == SessionAttachServer::ProbeStatus::Error)
    {
        return Result<void, Error>::err(Error::io(
            probe_error.empty() ? "Failed to check whether the selected session is running." : probe_error));
    }

    std::string load_error;
    auto target_state = load_session_state(target_id, &load_error);
    if (!target_state)
    {
        return Result<void, Error>::err(load_error.empty()
                ? Error::not_found("Saved session '" + target_id + "' was not found.")
                : Error::io(load_error));
    }
    if (target_state->workspaces.empty())
        return Result<void, Error>::err(Error::invalid_argument("Saved session has no workspaces."));

    auto previous_state = snapshot_session_state();
    if (!previous_state)
    {
        return Result<void, Error>::err(
            Error::invalid_argument("Current session could not be snapshotted before loading another session."));
    }

    std::string save_error;
    if (!save_session_state(*previous_state, &save_error))
    {
        return Result<void, Error>::err(
            Error::io(save_error.empty() ? "Failed to save the current session before loading." : save_error));
    }
    save_error.clear();
    if (!save_session_runtime_metadata(snapshot_session_runtime_metadata(false), &save_error))
    {
        return Result<void, Error>::err(
            Error::io(save_error.empty() ? "Failed to save current session metadata before loading." : save_error));
    }

    const std::string old_id = options_.session_id;
    const std::string old_option_name = options_.session_name;
    const std::string old_session_name = session_name_;
    const int64_t old_last_attached_unix_s = session_last_attached_unix_s_;
    const int64_t old_last_detached_unix_s = session_last_detached_unix_s_;
    const bool old_detached = detached_;
    const bool old_session_killed = session_killed_;

    auto rollback = [&]() {
        options_.session_id = old_id;
        options_.session_name = old_option_name;
        session_name_ = old_session_name;
        session_last_attached_unix_s_ = old_last_attached_unix_s;
        session_last_detached_unix_s_ = old_last_detached_unix_s;
        detached_ = old_detached;
        session_killed_ = old_session_killed;
        if (options_.enable_session_attach)
        {
            std::string restart_error;
            if (!start_session_attach_server(&restart_error))
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "Failed to restore session attach server for '%s' after load rollback: %s",
                    options_.session_id.c_str(),
                    restart_error.empty() ? "unknown error" : restart_error.c_str());
            }
        }
        const int pw = window_ ? window_->width_pixels() : last_pixel_w_;
        const int th = diagnostics_host_ ? diagnostics_host_->layout().terminal_height
                                         : (window_ ? window_->height_pixels() : last_pixel_h_);
        const int tab_y = chrome_host_ ? chrome_host_->tab_bar_height() : 0;
        const int host_h = std::max(1, th - tab_y);
        input_dispatcher_.set_host(nullptr);
        if (restore_session_state(pw, host_h, *previous_state))
        {
            recompute_all_viewports(0, tab_y, pw, host_h);
            input_dispatcher_.set_host(active_host_manager().focused_host());
            request_frame();
        }
    };

    options_.session_id = target_id;
    options_.session_name = target_state->session_name.empty() ? target_id : target_state->session_name;
    session_name_ = options_.session_name;
    detached_ = false;
    session_killed_ = false;
    session_last_attached_unix_s_ = 0;
    session_last_detached_unix_s_ = 0;
    if (auto metadata = load_session_runtime_metadata(target_id))
    {
        if (!metadata->session_name.empty())
        {
            options_.session_name = metadata->session_name;
            session_name_ = metadata->session_name;
        }
        session_last_attached_unix_s_ = metadata->last_attached_unix_s;
        session_last_detached_unix_s_ = metadata->last_detached_unix_s;
    }
    mark_session_attached();

    if (options_.enable_session_attach)
    {
        std::string start_error;
        if (!start_session_attach_server(&start_error))
        {
            rollback();
            return Result<void, Error>::err(Error::io(
                start_error.empty() ? "Failed to start the selected session attach server." : start_error));
        }
    }

    const int pw = window_ ? window_->width_pixels() : last_pixel_w_;
    const int th = diagnostics_host_ ? diagnostics_host_->layout().terminal_height
                                     : (window_ ? window_->height_pixels() : last_pixel_h_);
    const int tab_y = chrome_host_ ? chrome_host_->tab_bar_height() : 0;
    const int host_h = std::max(1, th - tab_y);
    input_dispatcher_.set_host(nullptr);
    if (!restore_session_state(pw, host_h, *target_state))
    {
        rollback();
        return Result<void, Error>::err(Error::io("Failed to restore the selected session."));
    }

    recompute_all_viewports(0, tab_y, pw, host_h);
    input_dispatcher_.set_host(active_host_manager().focused_host());
    persist_session_state();
    persist_session_runtime_metadata(options_.enable_session_attach);
    last_session_checkpoint_time_ = std::chrono::steady_clock::now();
    request_frame();
    return Result<void, Error>::ok();
}

Result<std::string, Error> App::save_session_as(std::string_view raw_name)
{
    PERF_MEASURE();
    const std::string display_name = trim_session_name(raw_name);
    if (display_name.empty())
        return Result<std::string, Error>::err(Error::invalid_argument("Enter a session name."));
    if (!options_.enable_session_restore)
    {
        return Result<std::string, Error>::err(
            Error::invalid_argument("Session restore is not enabled for this launch."));
    }
    if (!can_snapshot_session_state())
    {
        return Result<std::string, Error>::err(
            Error::invalid_argument("Current panes cannot be saved as a restorable shell session."));
    }

    auto new_id_result = make_unique_session_id(display_name, unix_now_seconds());
    if (!new_id_result)
        return Result<std::string, Error>::err(new_id_result.error());
    const std::string new_id = *new_id_result;

    const std::string old_id = options_.session_id;
    const std::string old_option_name = options_.session_name;
    const std::string old_session_name = session_name_;
    const int64_t old_last_attached_unix_s = session_last_attached_unix_s_;
    const int64_t old_last_detached_unix_s = session_last_detached_unix_s_;

    auto delete_new_files = [&]() {
        std::string delete_error;
        if (!delete_session_state(new_id, &delete_error) && !delete_error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to delete rolled-back session state for %s: %s",
                new_id.c_str(),
                delete_error.c_str());
        }
        delete_error.clear();
        if (!delete_session_runtime_metadata(new_id, &delete_error) && !delete_error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to delete rolled-back session metadata for %s: %s",
                new_id.c_str(),
                delete_error.c_str());
        }
    };

    auto rollback = [&]() {
        options_.session_id = old_id;
        options_.session_name = old_option_name;
        session_name_ = old_session_name;
        session_last_attached_unix_s_ = old_last_attached_unix_s;
        session_last_detached_unix_s_ = old_last_detached_unix_s;
        delete_new_files();
        if (options_.enable_session_attach)
        {
            std::string restart_error;
            if (!start_session_attach_server(&restart_error))
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "Failed to restore session attach server for '%s' after save-as rollback: %s",
                    options_.session_id.c_str(),
                    restart_error.empty() ? "unknown error" : restart_error.c_str());
            }
        }
    };

    options_.session_id = new_id;
    options_.session_name = display_name;
    session_name_ = display_name;
    mark_session_attached();

    if (options_.enable_session_attach)
    {
        std::string start_error;
        if (!start_session_attach_server(&start_error))
        {
            rollback();
            return Result<std::string, Error>::err(Error::io(
                start_error.empty() ? "Failed to start the session attach server." : start_error));
        }
    }

    auto state = snapshot_session_state();
    if (!state)
    {
        rollback();
        return Result<std::string, Error>::err(
            Error::invalid_argument("Current session could not be snapshotted."));
    }

    std::string save_error;
    if (!save_session_state(*state, &save_error))
    {
        rollback();
        return Result<std::string, Error>::err(
            Error::io(save_error.empty() ? "Failed to save named session." : save_error));
    }

    save_error.clear();
    if (!save_session_runtime_metadata(
            snapshot_session_runtime_metadata(options_.enable_session_attach), &save_error))
    {
        rollback();
        return Result<std::string, Error>::err(
            Error::io(save_error.empty() ? "Failed to save named session metadata." : save_error));
    }

    if (old_id != new_id)
    {
        std::string liveness_error;
        if (!clear_session_runtime_liveness(old_id, &liveness_error) && !liveness_error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to clear old session liveness for %s after save-as: %s",
                old_id.c_str(),
                liveness_error.c_str());
        }
    }

    request_frame();
    return new_id;
}

std::optional<AppSessionState> App::snapshot_session_state() const
{
    PERF_MEASURE();
    if (!can_snapshot_session_state())
        return std::nullopt;

    AppSessionState state;
    state.session_id = options_.session_id;
    state.session_name = session_name_.empty() ? options_.session_id : session_name_;
    state.active_workspace_id = active_workspace_;
    state.next_workspace_id = next_workspace_id_;

    for (const auto& ws : workspaces_)
    {
        auto host_manager_state = ws->host_manager.session_state();
        if (!host_manager_state)
            return std::nullopt;

        WorkspaceSessionState workspace_state;
        workspace_state.id = ws->id;
        workspace_state.name = ws->name;
        workspace_state.name_user_set = ws->name_user_set;
        workspace_state.host_manager = std::move(*host_manager_state);
        state.workspaces.push_back(std::move(workspace_state));
    }

    return state;
}

void App::persist_session_state()
{
    PERF_MEASURE();
    if (!options_.enable_session_restore || session_killed_)
        return;

    auto state = snapshot_session_state();
    if (!state)
        return;

    std::string error;
    if (!save_session_state(*state, &error))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to save shell session state: %s",
            error.empty() ? "unknown error" : error.c_str());
    }
}

SessionRuntimeMetadata App::snapshot_session_runtime_metadata(bool live) const
{
    SessionRuntimeMetadata metadata;
    metadata.session_id = options_.session_id;
    metadata.session_name = session_name_.empty() ? options_.session_id : session_name_;
    metadata.live = live;
    metadata.detached = live && detached_;
    metadata.owner_pid = live ? current_process_id() : 0;
    metadata.last_attached_unix_s = session_last_attached_unix_s_;
    metadata.last_detached_unix_s = session_last_detached_unix_s_;
    return metadata;
}

void App::persist_session_runtime_metadata(bool live)
{
    PERF_MEASURE();
    if (!options_.enable_session_restore || session_killed_ || !can_snapshot_session_state())
        return;

    std::string error;
    if (!save_session_runtime_metadata(snapshot_session_runtime_metadata(live), &error))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to save session metadata: %s",
            error.empty() ? "unknown error" : error.c_str());
    }
}

void App::mark_session_attached()
{
    session_last_attached_unix_s_ = unix_now_seconds();
}

void App::mark_session_detached()
{
    session_last_detached_unix_s_ = unix_now_seconds();
}

void App::maybe_checkpoint_session(std::chrono::steady_clock::time_point now)
{
    if (!options_.enable_session_restore || detached_ || session_killed_ || !can_snapshot_session_state())
        return;
    if (last_session_checkpoint_time_.time_since_epoch().count() != 0
        && now - last_session_checkpoint_time_ < options_.session_checkpoint_interval)
        return;

    persist_session_state();
    persist_session_runtime_metadata(options_.enable_session_attach);
    last_session_checkpoint_time_ = now;
}

SessionAttachServer::LiveSessionInfo App::live_session_info() const
{
    SessionAttachServer::LiveSessionInfo info;
    info.workspace_count = static_cast<int>(workspaces_.size());
    info.detached = detached_;
    info.owner_pid = current_process_id();
    info.last_attached_unix_s = session_last_attached_unix_s_;
    info.last_detached_unix_s = session_last_detached_unix_s_;
    for (const auto& ws : workspaces_)
        info.pane_count += ws->host_manager.host_count();
    return info;
}

bool App::restore_session_state(int pixel_w, int pixel_h, const AppSessionState& state)
{
    PERF_MEASURE();
    if (state.workspaces.empty())
        return false;

    for (auto& ws : workspaces_)
        ws->host_manager.shutdown();
    workspaces_.clear();
    active_workspace_ = -1;

    int max_workspace_id = -1;
    for (const WorkspaceSessionState& workspace_state : state.workspaces)
    {
        auto ws = std::make_unique<Workspace>(workspace_state.id, make_host_manager_deps());
        if (!ws->host_manager.restore_session_state(*this, pixel_w, pixel_h, workspace_state.host_manager))
        {
            for (auto& created_workspace : workspaces_)
                created_workspace->host_manager.shutdown();
            workspaces_.clear();
            active_workspace_ = -1;
            return false;
        }

        ws->initialized = true;
        ws->name = workspace_state.name.empty() ? "tab" : workspace_state.name;
        ws->name_user_set = workspace_state.name_user_set;
        max_workspace_id = std::max(max_workspace_id, workspace_state.id);
        workspaces_.push_back(std::move(ws));
    }

    next_workspace_id_ = std::max(state.next_workspace_id, max_workspace_id + 1);
    int restored_active_workspace = state.active_workspace_id;
    const bool has_restored_active_workspace = std::any_of(workspaces_.begin(), workspaces_.end(),
        [restored_active_workspace](const auto& ws) {
            return ws->id == restored_active_workspace;
        });
    if (!has_restored_active_workspace)
        restored_active_workspace = workspaces_.front()->id;
    activate_workspace(restored_active_workspace);
    return true;
}

bool App::create_initial_workspace(int pixel_w, int pixel_h)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    if (!ws->host_manager.create(*this, pixel_w, pixel_h))
    {
        last_init_error_ = ws->host_manager.error();
        return false;
    }
    ws->initialized = true;
    if (IHost* h = ws->host_manager.host())
        ws->name = h->debug_state().name;
    else
        ws->name = "tab";
    active_workspace_ = ws->id;
    workspaces_.push_back(std::move(ws));
    return true;
}

int App::add_workspace(int pixel_w, int pixel_h, std::optional<HostKind> host_kind)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    const HostKind kind = host_kind.value_or(HostManager::platform_default_split_host_kind());
    if (!ws->host_manager.create(*this, pixel_w, pixel_h, kind))
    {
        last_init_error_ = ws->host_manager.error();
        return -1;
    }
    ws->initialized = true;
    if (IHost* h = ws->host_manager.host())
        ws->name = h->debug_state().name;
    else
        ws->name = "tab";
    int id = ws->id;
    workspaces_.push_back(std::move(ws));
    activate_workspace(id);
    return id;
}

bool App::close_workspace(int workspace_id)
{
    if (workspaces_.size() <= 1)
        return false;
    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
        [workspace_id](const auto& ws) { return ws->id == workspace_id; });
    if (it == workspaces_.end())
        return false;

    (*it)->host_manager.shutdown();
    bool was_active = (workspace_id == active_workspace_);
    workspaces_.erase(it);
    if (was_active)
        activate_workspace(workspaces_.front()->id);
    return true;
}

void App::activate_workspace(int workspace_id)
{
    if (workspace_id == active_workspace_)
        return;
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_lost();
            break;
        }
    }
    active_workspace_ = workspace_id;
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_gained();
            break;
        }
    }
}

void App::next_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            activate_workspace(workspaces_[(i + 1) % workspaces_.size()]->id);
            return;
        }
    }
}

void App::prev_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            size_t prev = (i == 0) ? workspaces_.size() - 1 : i - 1;
            activate_workspace(workspaces_[prev]->id);
            return;
        }
    }
}

void App::move_workspace(int direction)
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            const auto n = static_cast<int>(workspaces_.size());
            const int target = (static_cast<int>(i) + direction + n) % n;
            std::swap(workspaces_[i], workspaces_[target]);
            return;
        }
    }
}

void App::activate_workspace_by_index(int one_based_index)
{
    const int idx = one_based_index - 1;
    if (idx < 0 || idx >= static_cast<int>(workspaces_.size()))
        return;
    activate_workspace(workspaces_[static_cast<size_t>(idx)]->id);
}

void App::activate_pane_by_index(int one_based_index)
{
    if (one_based_index <= 0)
        return;

    LeafId target = kInvalidLeaf;
    int current_index = 1;
    active_tree().for_each_leaf([&](LeafId id, const PaneDescriptor&) {
        if (target != kInvalidLeaf)
            return;
        if (current_index == one_based_index)
            target = id;
        ++current_index;
    });

    if (target != kInvalidLeaf)
        active_host_manager().set_focused(target);
}

void App::recompute_all_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    for (auto& ws : workspaces_)
        ws->host_manager.recompute_viewports(origin_x, origin_y, pixel_w, pixel_h);
}

HostManager& App::active_host_manager()
{
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    static HostManager dummy(HostManager::Deps{});
    return dummy;
}

const HostManager& App::active_host_manager() const
{
    for (const auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    static const HostManager dummy(HostManager::Deps{});
    return dummy;
}

const SplitTree& App::active_tree() const
{
    return active_host_manager().tree();
}

int App::workspace_count() const
{
    return static_cast<int>(workspaces_.size());
}

int App::active_workspace_id() const
{
    return active_workspace_;
}

void App::shutdown()
{
    PERF_MEASURE();
#ifdef __APPLE__
    macos_menu_.reset(); // tear down menu before handler goes away
#endif

    if (!session_killed_)
    {
        persist_session_state();
        persist_session_runtime_metadata(false);
    }
    session_attach_server_.stop();

    for (auto& ws : workspaces_)
        ws->host_manager.shutdown();
    workspaces_.clear();

    if (chrome_host_)
        chrome_host_->shutdown();
    host_owner_lifetime_.reset();

    if (options_.save_user_config && init_completed_)
    {
        init_completed_ = false; // prevent double-save on repeated shutdown() calls
        AppConfig config_to_save = config_;
        if (options_.load_user_config)
        {
            config_to_save = AppConfig::load();
            config_document_ = ConfigDocument::load();
        }
        auto [window_w, window_h] = window_->size_logical();
        if (window_w > 0 && window_h > 0)
        {
            config_to_save.window_width = window_w;
            config_to_save.window_height = window_h;
        }
        config_to_save.font_size = text_service_.point_size();
        config_to_save.markdown = config_.markdown;
        config_to_save.font_path = text_service_.primary_font_path();
        config_ = config_to_save;
        config_document_.merge_core_config(config_to_save);
        config_document_.save();
    }

    text_service_.shutdown();
    if (diagnostics_host_)
    {
        diagnostics_host_->shutdown();
        diagnostics_host_.reset();
    }
    if (renderer_.grid())
        renderer_.grid()->shutdown();
    if (window_)
        window_->shutdown();
}

} // namespace draxul

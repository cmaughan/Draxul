#include "app.h"
#include "server_status_surface.h"

#ifdef __APPLE__
#include "macos_menu.h"
#endif
#include "chrome_host.h"
#include "control_event_journal.h"
#include "control_request_router.h"
#include "gui_action_handler.h"
#include "input_dispatcher.h"
#include "pane_manager.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <draxul/atlas_upload.h>
#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/grid_host_base.h>
#include <draxul/log.h>
#include <draxul/pane_print.h>
#include <draxul/perf_timing.h>
#include <draxul/pixel_scale.h>
#include <draxul/remote_session_client.h>
#include <draxul/render_test_driver.h>
#include <draxul/sdl_window.h>
#include <draxul/server_client.h>
#include <filesystem>
#include <imgui.h>
#include <stdexcept>
#include <utility>

namespace draxul
{

namespace
{

// Fraction of the owner pane's height kept for its content when a Markdown
// preview pane is attached below it (Kanban card preview). The preview
// occupies the remaining bottom third.
constexpr float kMarkdownPreviewTopRatio = 2.0f / 3.0f;

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

template <typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F callback)
        : callback_(std::move(callback))
    {
    }
    ~ScopeExit()
    {
        callback_();
    }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    F callback_;
};

template <typename F>
ScopeExit(F) -> ScopeExit<F>;

class CallbackInputRouter final : public IInputRouter
{
public:
    std::function<IHost*()> overlay_host_fn;
    std::function<PaneManager*()> pane_manager_fn;
    std::function<int(int, int)> hit_test_space_fn;
    std::function<int(int, int)> hit_test_agent_fn;
    std::function<int(int, int)> hit_test_tab_fn;
    std::function<LeafId(int, int)> hit_test_pane_pill_fn;
    std::function<bool(int, int)> hit_test_app_chrome_fn;
    std::function<bool(int, int)> hit_test_shell_divider_fn;
    std::function<void(int)> resize_space_sidebar_fn;
    std::function<std::pair<int, int>()> cell_size_phys_fn;
    std::function<void(int)> activate_tab_fn;
    std::function<void(int)> activate_space_fn;
    std::function<void(int)> activate_agent_fn;
    std::function<void(int)> activate_pane_fn;
    std::function<void(int)> begin_space_rename_fn;
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

    PaneManager* pane_manager() override
    {
        return pane_manager_fn ? pane_manager_fn() : nullptr;
    }

    int hit_test_space(int phys_x, int phys_y) override
    {
        return hit_test_space_fn ? hit_test_space_fn(phys_x, phys_y) : kInvalidSpaceId;
    }

    int hit_test_agent(int phys_x, int phys_y) override
    {
        return hit_test_agent_fn ? hit_test_agent_fn(phys_x, phys_y) : 0;
    }

    int hit_test_tab(int phys_x, int phys_y) override
    {
        return hit_test_tab_fn ? hit_test_tab_fn(phys_x, phys_y) : 0;
    }

    LeafId hit_test_pane_pill(int phys_x, int phys_y) override
    {
        return hit_test_pane_pill_fn ? hit_test_pane_pill_fn(phys_x, phys_y) : kInvalidLeaf;
    }

    bool hit_test_app_chrome(int phys_x, int phys_y) override
    {
        return hit_test_app_chrome_fn && hit_test_app_chrome_fn(phys_x, phys_y);
    }

    bool hit_test_shell_divider(int phys_x, int phys_y) override
    {
        return hit_test_shell_divider_fn && hit_test_shell_divider_fn(phys_x, phys_y);
    }

    void resize_space_sidebar(int phys_x) override
    {
        if (resize_space_sidebar_fn)
            resize_space_sidebar_fn(phys_x);
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

    void activate_space(int space_id) override
    {
        if (activate_space_fn)
            activate_space_fn(space_id);
    }

    void activate_agent(int one_based_index) override
    {
        if (activate_agent_fn)
            activate_agent_fn(one_based_index);
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

    void begin_space_rename(int space_id) override
    {
        if (begin_space_rename_fn)
            begin_space_rename_fn(space_id);
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
    , space_controller_(std::filesystem::path(options_.host_working_dir))
{
    if (deps.http_client)
        weather_service_.set_http_client(std::move(deps.http_client));
    // PaneManager reads options_.host_factory to create hosts.  Sync our
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
        rebuild_agent_definitions();
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

    // Claim the Session BEFORE restoring it. initialize_chrome_host() respawns
    // the saved topology, so a second instance that only discovered the clash
    // afterwards would have already started duplicate shells for a Session it
    // is about to refuse.
    if (options_.enable_control_server)
    {
        control_server_ = std::make_unique<ControlServer>();
        std::string control_error;
        const auto runtime_directory = control_runtime_directory(
            ConfigDocument::default_path().parent_path());
        if (!control_server_->start(options_.session_id, runtime_directory, [this]() { wake_window(); }, &control_error))
        {
            if (control_server_->endpoint_in_use())
            {
                // Sessions are single-owner: two processes on one id restore
                // the same topology twice and then overwrite each other's
                // checkpoint, last writer winning. Refuse rather than corrupt.
                last_init_error_ = "Session '" + options_.session_id
                    + "' is already open in another Draxul window. Use "
                      "--session <id> to open a different one, or "
                      "--new-session to start a fresh one.";
                return false;
            }
            // Any other failure (an over-long endpoint path, an unwritable
            // runtime directory) costs only the automation surface. The
            // control plane is optional tooling and must never veto startup.
            DRAXUL_LOG_WARN(LogCategory::App,
                "Session control endpoint unavailable (%s); continuing without it",
                control_error.c_str());
            push_toast(1, "Automation endpoint unavailable: " + control_error);
            control_server_.reset();
        }
    }

    if (!time_step("Host", [this]() { return initialize_chrome_host(); }))
        return false;

    if (active_pane_manager().host())
        diagnostics_collector_.amend_last_step_label("Host (" + active_pane_manager().host()->debug_state().name + ")");

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
    init_completed_ = true;
    rollback.armed = false;
    return true;
}

TextServiceConfig App::make_text_service_config(const AppConfig& config) const
{
    TextServiceConfig text_config;
    text_config.font_path = config.font_path;
    text_config.bold_font_path = config.bold_font_path;
    text_config.italic_font_path = config.italic_font_path;
    text_config.bold_italic_font_path = config.bold_italic_font_path;
    text_config.fallback_paths = config.fallback_paths;
    text_config.enable_ligatures = config.enable_ligatures;
    return text_config;
}

bool App::initialize_text_service()
{
    PERF_MEASURE();
    display_ppi_ = options_.override_display_ppi.value_or(window_->display_ppi());

    if (const TextServiceConfig text_config = make_text_service_config(config_);
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

void App::apply_font_metrics()
{
    PERF_MEASURE();
    const auto& metrics = text_service_.metrics();
    renderer_.grid()->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_.grid()->set_ascender(metrics.ascender);
    const float imgui_font_size = imgui_font_size_from_metrics(metrics);
    diagnostics_host_->set_imgui_font(text_service_.primary_font_path(), imgui_font_size);
    for (const auto& space : space_controller_.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            tab->pane_manager.for_each_host([this, imgui_font_size](LeafId, IHost& host) {
                host.set_imgui_font(text_service_.primary_font_path(), imgui_font_size);
                host.on_font_metrics_changed();
            });
        }
    }
    refresh_app_shell_layout();
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

    const std::filesystem::path path = ConfigDocument::default_path();
    auto loaded_config = load_app_config_from_path_checked(path);
    if (!loaded_config)
        return Result<void, Error>::err(std::move(loaded_config).error());
    auto loaded_document = load_config_document_from_path_checked(path);
    if (!loaded_document)
        return Result<void, Error>::err(std::move(loaded_document).error());

    AppConfig reloaded_config = std::move(*loaded_config);
    apply_overrides(reloaded_config, options_.config_overrides);

    const AppConfig previous_config = config_;
    const bool text_config_needs_reload = text_service_config_changed(previous_config, reloaded_config);
    const bool scroll_config_changed = previous_config.smooth_scroll != reloaded_config.smooth_scroll
        || previous_config.scroll_speed != reloaded_config.scroll_speed;
    const bool weather_config_changed = previous_config.weather_location != reloaded_config.weather_location;

    // Validate the complete font replacement before publishing any field of
    // the candidate config. Moving a successful TextService keeps the object
    // address stable for renderer/host pointers and makes failure all-or-old.
    std::optional<TextService> staged_text_service;
    if (text_config_needs_reload)
    {
        TextService candidate;
        const TextServiceConfig text_config = make_text_service_config(reloaded_config);
        if (!candidate.initialize(text_config, reloaded_config.font_size, display_ppi_))
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to validate reloaded font settings from %s; retaining the previous config.",
                path.string().c_str());
            return Result<void, Error>::err(Error::config_apply(
                "Failed to apply reloaded font settings; previous config remains active."));
        }
        staged_text_service.emplace(std::move(candidate));
    }

    config_ = std::move(reloaded_config);
    config_document_ = std::move(*loaded_document);
    rebuild_agent_definitions();

    if (staged_text_service)
    {
        text_service_ = std::move(*staged_text_service);
        if (renderer_.imgui())
            renderer_.imgui()->rebuild_imgui_font_texture();
        apply_font_metrics();
        for (auto& warning : text_service_.take_font_warnings())
            push_toast(1, warning);
    }

    if (scroll_config_changed)
        input_dispatcher_.set_scroll_config(config_.smooth_scroll, config_.scroll_speed);
    input_dispatcher_.set_chord_indicator_fade_ms(config_.chord_indicator_fade_ms);

    if (weather_config_changed)
    {
        weather_service_.stop();
        if (!config_.weather_location.empty())
            weather_service_.start(config_.weather_location);
    }

    for (const auto& warning : config_.warnings)
        push_toast(0, warning);

    if (!staged_text_service
        && previous_config.space_sidebar_columns != config_.space_sidebar_columns)
        refresh_app_shell_layout();

    const HostReloadConfig host_reload = host_reload_config_from_app_config(config_);
    for (const auto& space : space_controller_.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            tab->pane_manager.for_each_host([&host_reload](LeafId, IHost& host) {
                host.on_config_reloaded(host_reload);
            });
        }
    }

    request_frame();
    DRAXUL_LOG_INFO(LogCategory::App, "Reloaded config from %s",
        ConfigDocument::default_path().string().c_str());
    return Result<void, Error>::ok();
}

bool App::initialize_chrome_host()
{
    PERF_MEASURE();
    host_owner_lifetime_ = std::make_shared<int>(0);
    refresh_system_resource_snapshot(std::chrono::steady_clock::now());

    ChromeHost::Deps chrome_deps;
    chrome_deps.config = &config_;
    chrome_deps.grid_renderer = renderer_.grid();
    chrome_deps.text_service = &text_service_;
    chrome_deps.space_controller = &space_controller_;
    chrome_deps.agent_controller = &agent_controller_;
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
    chrome_deps.set_tab_name = [this](int tab_id, std::string name) {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::RenameTab,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = tab_id,
            .name = std::move(name),
        });
        if (!result.accepted())
        {
            push_toast(2, result.error.empty() ? "Could not rename the tab." : result.error);
        }
    };
    chrome_deps.set_space_name = [this](
                                     SpaceId space_id,
                                     std::string name) {
        if (auto renamed = rename_space(space_id, name); !renamed)
        {
            push_toast(2, renamed.error().message.empty()
                    ? "Could not rename the Space."
                    : renamed.error().message);
        }
    };
    chrome_deps.set_pane_name = [this](LeafId leaf, std::string name) {
        // Apply to whichever tab currently owns the leaf — pane edits
        // are always against the active tab.
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::RenamePane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id = leaf,
            .name = std::move(name),
        });
        if (!result.accepted())
        {
            push_toast(2, result.error.empty() ? "Could not rename the pane." : result.error);
        }
    };
    chrome_deps.get_pane_name = [this](LeafId leaf) {
        return active_pane_manager().pane_name(leaf);
    };
    chrome_deps.get_pane_display_name = [this](LeafId leaf) {
        return active_pane_manager().pane_display_name(leaf);
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
                saved_session && !saved_session->spaces.empty()
                && std::any_of(saved_session->spaces.begin(), saved_session->spaces.end(),
                    [](const SpaceSnapshot& space) { return !space.tabs.empty(); }))
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

    // A server-owned Session must create its first panes directly from the
    // authoritative topology. Creating a temporary RemoteTerminalHost here
    // would attach to the legacy default terminal identity before topology is
    // known, which fails for restored Sessions whose surviving terminals have
    // different stable IDs.
    if (!restored_session
        && !options_.enable_remote_topology
        && !create_initial_tab(window_->width_pixels(), diagnostics_host_->layout().terminal_height))
    {
        // Clean up stale session state that may have contributed to the failure,
        // but don't retry — if the host can't be created (e.g. nvim not on PATH),
        // retrying won't help and makes CI smoke tests fail harder.
        delete_session_state(options_.session_id);
        return false;
    }

    if (options_.enable_remote_topology
        && !initialize_remote_topology())
    {
        return false;
    }
    initialize_topology_mutation_route();

    refresh_app_shell_layout();

    const float font_size = imgui_font_size_from_metrics(text_service_.metrics());
    if (IHost* host = active_pane_manager().host())
        host->set_imgui_font(
            text_service_.primary_font_path(), font_size);

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
    gui_deps.focused_host = [this]() { return active_pane_manager().focused_host(); };
    gui_deps.imgui_host = renderer_.imgui();
    gui_deps.config = &config_;
    gui_deps.on_font_changed = [this]() { apply_font_metrics(); };
    gui_deps.on_open_file_dialog = [this]() { window_->show_open_file_dialog(); };
    gui_deps.on_split_vertical = [this](std::optional<HostKind> kind) {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::SplitPane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id
            = active_pane_manager().focused_leaf(),
            .direction
            = TopologySplitDirection::Vertical,
            .host_kind = kind,
        });
        if (!result.accepted())
        {
            push_toast(2, result.error.empty() ? "Failed to spawn split pane." : result.error);
        }
    };
    gui_deps.on_split_horizontal = [this](std::optional<HostKind> kind) {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::SplitPane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id
            = active_pane_manager().focused_leaf(),
            .direction
            = TopologySplitDirection::Horizontal,
            .host_kind = kind,
        });
        if (!result.accepted())
        {
            push_toast(2, result.error.empty() ? "Failed to spawn split pane." : result.error);
        }
    };
    gui_deps.on_panel_toggled = [this]() {
        refresh_app_shell_layout();
        update_diagnostics_panel();
        request_frame();
    };
    gui_deps.on_command_palette = [this]() {
        if (palette_host_)
            palette_host_->dispatch_action("toggle");
    };
    gui_deps.on_quit = [this]() { request_quit(); };
    gui_deps.on_server_status = [this]() {
        if (!options_.server_connection
            || options_.server_runtime_directory.empty())
        {
            push_toast(1,
                "This window is using the local terminal runtime.");
            return;
        }
        if (!remote_session_client_)
        {
            push_toast(2,
                "Draxul server status worker is unavailable.");
            return;
        }
        const auto request_id
            = remote_session_client_->request_status();
        if (!request_id)
        {
            push_toast(1,
                "A Draxul server status request is already pending.");
            return;
        }
        pending_server_status_actions_[*request_id]
            = PendingServerStatusAction::ShowStatus;
    };
    gui_deps.on_open_server_log = [this]() {
        if (options_.server_runtime_directory.empty())
        {
            push_toast(1,
                "This window has no shared server log.");
            return;
        }
        std::string error;
        if (!open_server_log(
                default_server_log_path(
                    options_.server_runtime_directory),
                error))
        {
            push_toast(2, error);
        }
    };
    gui_deps.on_stop_server = [this]() {
        open_stop_server_prompt();
    };
    gui_deps.on_new_space = [this]() { open_new_space_prompt(); };
    gui_deps.on_switch_space = [this]() { open_switch_space_picker(); };
    gui_deps.on_rename_space = [this]() { open_rename_space_prompt(); };
    gui_deps.on_close_space = [this]() {
        const Space* active = space_controller_.find_active_space();
        const std::string name = active ? active->name : std::string("space");
        if (auto closed = close_space(space_controller_.active_space_id()); !closed)
            push_toast(2, closed.error().message);
        else
            push_toast(0, "Closed Space '" + name + "'.");
    };
    gui_deps.on_launch_agent = [this]() { open_launch_agent_prompt(); };
    gui_deps.on_attach_agent_identity = [this]() {
        open_attach_agent_picker();
    };
    gui_deps.on_focus_agent = [this]() { open_focus_agent_picker(); };
    gui_deps.on_restart_agent = [this]() {
        const auto agents
            = agent_controller_.query(space_controller_);
        const auto focused = std::ranges::find_if(
            agents, [](const AgentProjection& agent) {
                return agent.focused;
            });
        if (focused == agents.end())
        {
            push_toast(2, "The focused pane does not contain a tracked agent.");
            return;
        }
        if (auto restarted
            = restart_agent_runtime(*focused);
            !restarted)
        {
            push_toast(2,
                restarted.error().message.empty()
                    ? "Failed to restart agent."
                    : restarted.error().message);
        }
    };
    gui_deps.on_clear_agent_identity = [this]() {
        if (!agent_controller_.dismiss_focused(space_controller_))
        {
            push_toast(2, "The focused pane has no tracked agent identity.");
            return;
        }
        mark_session_dirty();
        refresh_app_shell_layout();
        request_frame();
    };
    gui_deps.on_explain_agent_state = [this]() {
        const auto agents = agent_controller_.query(space_controller_);
        const auto it = std::find_if(agents.begin(), agents.end(),
            [](const AgentProjection& agent) { return agent.focused; });
        if (it == agents.end())
        {
            push_toast(2, "The focused pane does not contain a tracked agent.");
            return;
        }

        const AgentStatusExplanation& explanation = it->status_explanation;
        std::string message = it->identity.display_name + ": "
            + std::string(to_string(it->status)) + "; identity="
            + std::string(to_string(it->identity.origin)) + "/"
            + it->identity_evidence_category + "; authority="
            + std::string(to_string(it->status_authority));
        if (!explanation.manifest_id.empty())
        {
            message += "; manifest=" + explanation.manifest_id + "/v"
                + std::to_string(explanation.manifest_version);
        }
        if (!explanation.rule_id.empty())
            message += "; rule=" + explanation.rule_id;
        if (!explanation.evidence_category.empty())
            message += "; evidence=" + explanation.evidence_category;
        if (!explanation.fallback_reason.empty())
            message += "; fallback=" + explanation.fallback_reason;
        if (it->last_status_transition_at
            != std::chrono::steady_clock::time_point{})
        {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - it->last_status_transition_at);
            message += "; transition=" + std::to_string(std::max<int64_t>(0, age.count()))
                + "s ago";
        }
        push_toast(0, message);
    };
    gui_deps.on_edit_config = [this]() {
        if (remote_session_client_)
        {
            push_toast(1,
                "Edit-config launch arguments are not in shared topology yet.");
            return;
        }
        HostLaunchOptions launch;
        launch.kind = HostKind::Nvim;
        launch.args = { ConfigDocument::default_path().string() };
        LeafId new_leaf = active_pane_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
        else
        {
            const std::string& err = active_pane_manager().error();
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
        active_pane_manager().toggle_zoom(
            shell_layout_.work_area.w, shell_layout_.work_area.h);
        mark_session_dirty();
        refresh_app_shell_layout();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_close_pane = [this]() {
        if (active_pane_manager().host_count() > 1)
        {
            TopologyMutationResult result = mutate_topology({
                .kind = TopologyMutationKind::ClosePane,
                .space_id
                = space_controller_.active_space_id(),
                .tab_id = active_tab_id(),
                .pane_id
                = active_pane_manager().focused_leaf(),
            });
            if (!result.accepted())
            {
                push_toast(2, result.error);
            }
        }
        else if (tab_count() > 1)
        {
            if (!close_tab(active_tab_id()))
                push_toast(2, last_init_error_);
        }
        else if (space_controller_.count() > 1)
        {
            if (auto closed = close_space(
                    space_controller_.active_space_id());
                !closed)
            {
                push_toast(2, closed.error().message);
            }
        }
        else if (topology_mutation_route_
            && topology_mutation_route_->route_kind()
                == TopologyMutationRouteKind::ServerBacked)
        {
            // The final shared pane belongs to the server. Closing the
            // client detaches from it instead of deleting server state.
            running_ = false;
        }
        else
        {
            input_dispatcher_.set_host(nullptr);
            // Closing the last local pane discards this saved topology and
            // exits.
            discard_session_state_on_shutdown_ = true;
            delete_session_state(options_.session_id);
            space_controller_.shutdown_all();
            render_root_ = RenderNode{};
            running_ = false;
        }
        refresh_app_shell_layout();
        if (running_)
        {
            input_dispatcher_.set_host(
                active_pane_manager().focused_host());
        }
        request_frame();
    };
    gui_deps.on_restart_host = [this]() {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::RestartPane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id
            = active_pane_manager().focused_leaf(),
        });
        if (!result.accepted())
            push_toast(2, result.error);
    };
    gui_deps.on_swap_pane = [this]() {
        const LeafId focused
            = active_pane_manager().focused_leaf();
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::SwapPane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id = focused,
            .target_pane_id
            = active_tree().next_leaf_after(focused),
        });
        if (!result.accepted())
            push_toast(2, result.error);
    };
    auto focus_pane = [this](FocusDirection dir) {
        if (active_pane_manager().focus_direction(dir))
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
    };
    gui_deps.on_focus_left = [focus_pane]() { focus_pane(FocusDirection::Left); };
    gui_deps.on_focus_right = [focus_pane]() { focus_pane(FocusDirection::Right); };
    gui_deps.on_focus_up = [focus_pane]() { focus_pane(FocusDirection::Up); };
    gui_deps.on_focus_down = [focus_pane]() { focus_pane(FocusDirection::Down); };
    auto resize_pane = [this](FocusDirection dir) {
        PaneManager& hm = active_pane_manager();
        DividerId id = hm.find_focused_ancestor_divider(dir);
        if (id == kInvalidDivider)
            return;
        // Positive delta grows the first child (the leaf on the left/top side
        // of the divider). For Left/Up actions we shrink the first child;
        // for Right/Down we grow it.
        const float step = 0.05f;
        const float delta
            = (dir == FocusDirection::Right || dir == FocusDirection::Down) ? step : -step;
        const auto current = hm.divider_ratio(id);
        if (!current)
            return;
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::SetSplitRatio,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .divider_id = id,
            .ratio = std::clamp(
                *current + delta, 0.1f, 0.9f),
            .ratio_delta = delta,
        });
        if (!result.accepted())
        {
            push_toast(2, result.error);
        }
    };
    gui_deps.on_resize_pane_left = [resize_pane]() { resize_pane(FocusDirection::Left); };
    gui_deps.on_resize_pane_right = [resize_pane]() { resize_pane(FocusDirection::Right); };
    gui_deps.on_resize_pane_up = [resize_pane]() { resize_pane(FocusDirection::Up); };
    gui_deps.on_resize_pane_down = [resize_pane]() { resize_pane(FocusDirection::Down); };
    gui_deps.on_new_tab = [this](std::optional<HostKind> kind) {
        const int pw = window_->width_pixels();
        const int th = diagnostics_host_->layout().terminal_height;
        int id = add_tab(pw, th, kind);
        if (id >= 0)
        {
            // Set the font on the new host so ImGui uses the app's font, not the default.
            if (IHost* h = active_pane_manager().host())
            {
                const float font_size = imgui_font_size_from_metrics(text_service_.metrics());
                h->set_imgui_font(text_service_.primary_font_path(), font_size);
            }
            refresh_app_shell_layout();
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            request_frame();
        }
    };
    gui_deps.on_close_tab = [this]() {
        if (tab_count() <= 1)
            return;
        int closing = active_tab_id();
        input_dispatcher_.set_host(nullptr);
        close_tab(closing);
        refresh_app_shell_layout();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_next_tab = [this]() {
        next_tab();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_prev_tab = [this]() {
        prev_tab();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_activate_tab = [this](int index) {
        activate_tab_by_index(index);
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_rename_tab = [this]() {
        if (chrome_host_)
            chrome_host_->begin_tab_rename_by_id(active_tab_controller().active_tab_id());
        request_frame();
    };
    gui_deps.on_move_tab_left = [this]() {
        move_tab(-1);
        request_frame();
    };
    gui_deps.on_move_tab_right = [this]() {
        move_tab(1);
        request_frame();
    };
    gui_deps.on_duplicate_pane = [this]() {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::DuplicatePane,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id
            = active_pane_manager().focused_leaf(),
        });
        if (!result.accepted())
        {
            push_toast(2, result.error);
        }
    };
    gui_deps.on_equalize_panes = [this]() {
        TopologyMutationResult result = mutate_topology({
            .kind = TopologyMutationKind::EqualizeSplits,
            .space_id
            = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
        });
        if (!result.accepted())
        {
            push_toast(2, result.error);
        }
    };
    gui_deps.on_print_pane = [this]() { start_print_focused_pane(); };
    gui_deps.on_rename_pane = [this]() {
        if (!chrome_host_)
            return;
        const LeafId leaf = active_pane_manager().focused_leaf();
        if (leaf == kInvalidLeaf)
            return;
        chrome_host_->begin_pane_rename(leaf);
        request_frame();
    };
    gui_deps.broadcast_action = [this](std::string_view action) {
        active_pane_manager().for_each_host(
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

void App::open_new_space_prompt()
{
    if (!palette_host_)
        return;

    CommandPalette::PromptRequest request;
    request.title = "New Space";
    request.prompt = "Name";
    request.empty_message = "Enter a Space name";
    request.on_submit = [this](std::string name) {
        auto created = create_space(name);
        if (!created)
        {
            push_toast(2, created.error().message);
            return;
        }
        push_toast(0, "Created Space '" + name + "'.");
    };
    if (!palette_host_->open_prompt(std::move(request)))
        push_toast(2, "Unable to open Space name prompt.");
}

void App::open_switch_space_picker()
{
    if (!palette_host_)
        return;

    CommandPalette::ChoiceRequest request;
    request.title = "Switch Space";
    request.entries.reserve(space_controller_.spaces().size());
    for (const auto& space : space_controller_.spaces())
    {
        const bool active = space->id == space_controller_.active_space_id();
        const std::string root = space->root_directory.string();
        request.entries.push_back({
            .id = std::to_string(space->id),
            .name = space->name,
            .shortcut_hint = active ? "active" : root,
            .search_text = space->name + " " + root,
        });
    }
    request.on_submit = [this](std::string id_text) {
        try
        {
            const SpaceId id = std::stoi(id_text);
            if (auto activated = activate_space(id); !activated)
                push_toast(2, activated.error().message);
        }
        catch (const std::exception&)
        {
            push_toast(2, "Invalid Space selection.");
        }
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open Space picker.");
}

void App::open_rename_space_prompt()
{
    if (!palette_host_)
        return;
    const Space* active = space_controller_.find_active_space();
    if (!active)
        return;
    const SpaceId id = active->id;

    CommandPalette::PromptRequest request;
    request.title = "Rename Space";
    request.prompt = "Name";
    request.initial_value = active->name;
    request.empty_message = "Enter a Space name";
    request.on_submit = [this, id](std::string name) {
        if (auto renamed = rename_space(id, name); !renamed)
            push_toast(2, renamed.error().message);
    };
    if (!palette_host_->open_prompt(std::move(request)))
        push_toast(2, "Unable to open Space rename prompt.");
}

void App::open_stop_server_prompt()
{
    if (!palette_host_
        || !options_.server_connection
        || options_.server_runtime_directory.empty())
    {
        push_toast(1,
            "This window is not attached to the shared server.");
        return;
    }
    if (!remote_session_client_)
    {
        push_toast(2,
            "Draxul server status worker is unavailable.");
        return;
    }
    const auto request_id
        = remote_session_client_->request_status();
    if (!request_id)
    {
        push_toast(1,
            "A Draxul server status request is already pending.");
        return;
    }
    pending_server_status_actions_[*request_id]
        = PendingServerStatusAction::ConfirmStop;
}

void App::show_stop_server_prompt(
    const ServerStatusSnapshot& status)
{
    size_t live_terminals = 0;
    for (const auto& session : status.session_statuses)
    {
        live_terminals += session.live_terminals;
    }

    CommandPalette::ChoiceRequest request;
    request.title = "Stop Draxul Server?";
    request.entries.push_back({
        .id = "cancel",
        .name = "Cancel",
        .shortcut_hint = "keep terminals running",
        .search_text = "cancel keep running",
    });
    request.entries.push_back({
        .id = "stop",
        .name = "Stop Server",
        .shortcut_hint = live_terminals == 0
            ? "no live terminals"
            : "closes " + std::to_string(live_terminals)
                + " live terminal"
                + (live_terminals == 1 ? "" : "s"),
        .search_text = "stop shutdown server terminals",
    });
    request.on_submit = [this](std::string choice) {
        if (choice != "stop")
            return;
        std::string error;
        if (!ServerClient::shutdown(
                options_.server_runtime_directory,
                {
                    .confirm_live_terminals = true,
                    .request_timeout
                    = std::chrono::milliseconds(100),
                },
                error))
        {
            show_force_stop_server_prompt(
                std::move(error));
            return;
        }
        request_quit();
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open server shutdown confirmation.");
}

void App::show_force_stop_server_prompt(
    std::string graceful_error)
{
    if (!palette_host_)
        return;

    CommandPalette::ChoiceRequest request;
    request.title = "Graceful Stop Failed";
    request.entries.push_back({
        .id = "cancel",
        .name = "Cancel",
        .shortcut_hint = graceful_error.empty()
            ? "keep the server running"
            : graceful_error.substr(0, 120),
        .search_text = "cancel keep running",
    });
    request.entries.push_back({
        .id = "force",
        .name = "Force Stop Server",
        .shortcut_hint = "immediately destroys terminal processes",
        .search_text = "force stop server terminals",
    });
    request.on_submit = [this](std::string choice) {
        if (choice != "force")
            return;
        std::string error;
        if (!ServerClient::force_stop(
                options_.server_runtime_directory,
                true, error))
        {
            push_toast(2, error);
            return;
        }
        request_quit();
    };
    if (!palette_host_->open_choices(std::move(request)))
    {
        push_toast(2,
            graceful_error.empty()
                ? "Unable to open force-stop confirmation."
                : graceful_error);
    }
}

void App::handle_remote_status_completion(
    RemoteStatusCompletion completion)
{
    const auto pending
        = pending_server_status_actions_.find(
            completion.request_id);
    if (pending == pending_server_status_actions_.end())
        return;
    const PendingServerStatusAction action = pending->second;
    pending_server_status_actions_.erase(pending);

    if (!completion.result.ok
        || !completion.result.status)
    {
        push_toast(2,
            completion.result.error_message.empty()
                ? "Draxul server status is unavailable."
                : completion.result.error_message);
        return;
    }
    if (action == PendingServerStatusAction::ShowStatus)
    {
        push_toast(0, format_server_status_summary(*completion.result.status));
    }
    else
    {
        show_stop_server_prompt(*completion.result.status);
    }
}

void App::open_launch_agent_prompt()
{
    if (!palette_host_)
        return;

    CommandPalette::ChoiceRequest request;
    request.title = "Launch Agent";
    for (const AgentDefinition& definition : agent_definitions_.definitions())
    {
        request.entries.push_back({
            .id = definition.profile_id,
            .name = definition.display_name,
            .shortcut_hint = definition.executable,
            .search_text = definition.profile_id + " " + definition.kind + " "
                + definition.executable,
        });
    }
    request.on_submit = [this](std::string profile_id) {
        const AgentDefinition* definition = agent_definitions_.find(profile_id);
        auto launched = launch_agent(profile_id);
        if (!launched)
        {
            push_toast(2, launched.error().message);
            return;
        }
        push_toast(0, "Launched agent '" + (definition ? definition->display_name : profile_id) + "'.");
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open agent profile picker.");
}

void App::open_attach_agent_picker()
{
    if (!palette_host_)
        return;

    CommandPalette::ChoiceRequest request;
    request.title = "Attach Agent Identity";
    for (const AgentDefinition& definition : agent_definitions_.definitions())
    {
        request.entries.push_back({
            .id = definition.profile_id,
            .name = definition.display_name,
            .shortcut_hint = definition.kind,
            .search_text = definition.profile_id + " " + definition.kind + " "
                + definition.display_name,
        });
    }
    request.on_submit = [this](std::string profile_id) {
        const AgentDefinition* definition = agent_definitions_.find(profile_id);
        if (!definition
            || !agent_controller_.attach_focused(
                space_controller_, *definition))
        {
            push_toast(
                2, "Unable to attach an agent identity to the focused pane.");
            return;
        }
        push_toast(
            0, "Attached '" + definition->display_name + "' to the focused pane.");
        refresh_app_shell_layout();
        request_frame();
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open agent identity picker.");
}

void App::open_focus_agent_picker()
{
    if (!palette_host_)
        return;

    const auto agents = agent_controller_.query(space_controller_);
    CommandPalette::ChoiceRequest request;
    request.title = "Focus Agent";
    for (const AgentProjection& agent : agents)
    {
        const Space* space = space_controller_.find_space(agent.space_id);
        request.entries.push_back({
            .id = agent.identity.instance_id,
            .name = agent.identity.display_name,
            .shortcut_hint = space ? space->name : std::string{},
            .search_text = agent.identity.display_name + " " + agent.identity.kind + " "
                + (space ? space->name : std::string{}),
        });
    }
    request.on_submit = [this](std::string instance_id) {
        if (!agent_controller_.focus(space_controller_, instance_id))
        {
            push_toast(2, "Agent is no longer available.");
            return;
        }
        refresh_app_shell_layout();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open agent picker.");
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
    disp_deps.host = active_pane_manager().host();
    router->pane_manager_fn = [this]() -> PaneManager* { return &active_pane_manager(); };
    disp_deps.smooth_scroll = config_.smooth_scroll;
    disp_deps.scroll_speed = config_.scroll_speed;
    disp_deps.pixel_scale = PixelScale::from_window(window_->width_pixels(), window_->width_logical());
    disp_deps.request_frame = [this]() { request_frame(); };
    disp_deps.on_layout_changed = [this]() { mark_session_dirty(); };
    disp_deps.on_resize = [this](int w, int h) { on_resize(w, h); };
    disp_deps.on_display_scale_changed = [this](float ppi) { on_display_scale_changed(ppi); };
    router->hit_test_tab_fn = [this](int px, int py) { return chrome_host_ ? chrome_host_->hit_test_tab(px, py) : 0; };
    router->hit_test_space_fn = [this](int px, int py) {
        return chrome_host_ ? chrome_host_->hit_test_space(px, py) : kInvalidSpaceId;
    };
    router->hit_test_agent_fn = [this](int px, int py) {
        return chrome_host_ ? chrome_host_->hit_test_agent(px, py) : 0;
    };
    router->hit_test_app_chrome_fn = [this](int px, int py) {
        return hit_test_app_chrome(px, py);
    };
    router->hit_test_shell_divider_fn = [this](int px, int py) {
        return hit_test_shell_divider(px, py);
    };
    router->resize_space_sidebar_fn = [this](int px) {
        resize_space_sidebar_to_pixel(px);
    };
    router->cell_size_phys_fn = [this]() {
        return renderer_.grid() ? renderer_.grid()->cell_size_pixels() : std::pair<int, int>{ 0, 0 };
    };
    router->activate_tab_fn = [this](int index) {
        activate_tab_by_index(index);
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    router->activate_space_fn = [this](int id) {
        if (auto activated = activate_space(static_cast<SpaceId>(id)); !activated)
            push_toast(2, activated.error().message);
    };
    router->activate_agent_fn = [this](int index) {
        if (!agent_controller_.focus_by_index(space_controller_, index))
        {
            push_toast(2, "Agent is no longer available.");
            return;
        }
        refresh_app_shell_layout();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        mark_session_dirty();
        request_frame();
    };
    router->activate_pane_fn = [this](int index) {
        activate_pane_by_index(index);
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    router->begin_tab_rename_fn = [this](int tab_index) {
        // Activate the tab before editing so the user always edits the
        // visually-active tab.
        activate_tab_by_index(tab_index);
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        chrome_host_->begin_tab_rename(tab_index);
        request_frame();
    };
    router->begin_space_rename_fn = [this](int space_id) {
        if (auto activated = activate_space(
                static_cast<SpaceId>(space_id));
            !activated)
        {
            push_toast(2, activated.error().message);
            return;
        }
        if (chrome_host_)
        {
            chrome_host_->begin_space_rename(
                static_cast<SpaceId>(space_id));
        }
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
    input_dispatcher_.reconfigure(std::move(disp_deps));
    input_dispatcher_.set_chord_indicator_fade_ms(config_.chord_indicator_fade_ms);
    input_dispatcher_.connect(*window_);
    IWindow::LifecycleCallbacks lifecycle_callbacks;
    lifecycle_callbacks.on_close_requested = [this]() { on_window_close_requested(); };
    lifecycle_callbacks.on_quit_requested = [this]() { request_quit(); };
    window_lifecycle_connection_ = window_->connect_lifecycle_callbacks(
        std::move(lifecycle_callbacks));
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
        const Tab* tab = find_active_tab();
        if (tab != nullptr && tab->pane_manager.host()
            && tab->pane_manager.host()->runtime_state().content_ready && saw_frame_)
            return true;
    }
    return false;
}

bool App::dispatch_gui_action(std::string_view action)
{
    return gui_action_handler_.execute(action);
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

void App::start_print_focused_pane()
{
    if (find_active_tab() == nullptr)
    {
        push_toast(1, "No focused pane to print");
        return;
    }
    if (renderer_.capture() == nullptr)
    {
        push_toast(2, "Printing is not supported by this renderer");
        return;
    }
    const SplitTree& tree = active_tree();
    const LeafId focused = tree.focused();
    if (focused == kInvalidLeaf)
    {
        push_toast(1, "No focused pane to print");
        return;
    }
    print_pane_rect_ = tree.descriptor_for(focused);
    if (print_pane_rect_.pixel_size.x <= 0 || print_pane_rect_.pixel_size.y <= 0)
    {
        push_toast(1, "The focused pane has no visible area");
        return;
    }
    IHost* focused_host = active_pane_manager().focused_host();
    print_hint_ = focused_host != nullptr ? focused_host->print_hint() : HostPrintHint{};
    print_capture_pending_ = true;
    renderer_.capture()->request_frame_capture();
    request_frame();
}

void App::finish_print_capture(const CapturedFrame& frame)
{
    // The host's print hint narrows the crop to actual content (ScoreView:
    // the page/band, not the backdrop) — pane-relative, so offset it.
    glm::ivec2 crop_pos = print_pane_rect_.pixel_pos;
    glm::ivec2 crop_size = print_pane_rect_.pixel_size;
    if (print_hint_.content_size.x > 0 && print_hint_.content_size.y > 0)
    {
        crop_pos += print_hint_.content_pos;
        crop_size = glm::min(print_hint_.content_size,
            print_pane_rect_.pixel_size - print_hint_.content_pos);
    }
    CroppedImage pane = crop_rgba(frame.rgba, frame.width, frame.height, crop_pos.x,
        crop_pos.y, crop_size.x, crop_size.y);
    if (pane.rgba.empty())
    {
        push_toast(2, "Print capture missed the pane");
        return;
    }
    if (print_hint_.paper_white)
        snap_paper_white(pane);

    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::filesystem::path pdf_path = std::filesystem::temp_directory_path() / ("draxul-pane-" + std::to_string(stamp) + ".pdf");

    std::string error;
    if (!write_rgba_pdf_a4(pane.rgba.data(), pane.width, pane.height, pdf_path, error))
    {
        push_toast(2, "Print failed: " + error);
        return;
    }
    DRAXUL_LOG_INFO(LogCategory::App, "print_pane: %dx%d pane -> %s", pane.width, pane.height,
        pdf_path.string().c_str());

    // Test/verification hook: compose the PDF but skip the print dialog.
    if (std::getenv("DRAXUL_PRINT_DRY_RUN") != nullptr)
    {
        push_toast(0, "Print dry run: " + pdf_path.string());
        return;
    }
    switch (present_print_dialog_for_pdf(pdf_path, error))
    {
    case PrintDialogResult::Printed:
        push_toast(0, "Pane sent to printer");
        break;
    case PrintDialogResult::Canceled:
        push_toast(0, "Print canceled");
        break;
    case PrintDialogResult::Failed:
        push_toast(2, "Print failed: " + error);
        break;
    }
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
        const Tab* tab = find_active_tab();
        if (tab != nullptr)
        {
            if (auto* host = tab->pane_manager.host())
                return host->runtime_state();
        }
        return std::nullopt;
    };
    env.capture = [this]() { return renderer_.capture(); };
    env.enable_diagnostics_panel = [this]() {
        diagnostics_host_->set_visible(true);
        refresh_app_shell_layout();
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
    if (find_active_tab() == nullptr)
        return false;
    std::vector<LeafId> dead;
    bool clean_final_remote_exit = false;
    active_pane_manager().for_each_host([this, &dead, &clean_final_remote_exit](LeafId id, const IHost& h) {
        const std::string pane_id = active_pane_manager().pane_id(id);
        const bool server_owned_remote_terminal
            = active_pane_manager()
                  .is_server_owned_remote_terminal_leaf(id);
        // RemoteTerminalHost::is_running() describes the server transport,
        // which deliberately remains alive after its shell process exits.
        // Inspect the process outcome before taking that transport fast path.
        if (server_owned_remote_terminal
            && h.exit_code() == 0
            && active_pane_manager().host_count() == 1
            && tab_count() == 1
            && space_controller_.count() == 1)
        {
            clean_final_remote_exit = true;
            return;
        }
        if (h.is_running())
        {
            if (!pane_id.empty())
                announced_dead_panes_.erase(pane_id);
            return;
        }
        if (server_owned_remote_terminal)
        {
            // The server publishes terminal exit as a topology update. Keep
            // this projection intact until that authoritative update arrives
            // instead of attempting a local close which the projected layout
            // deliberately rejects.
            return;
        }
        if (active_pane_manager().should_preserve_dead_leaf(id))
        {
            if (!pane_id.empty() && announced_dead_panes_.insert(pane_id).second)
            {
                const std::string pane_label = active_pane_manager().pane_name(id).empty()
                    ? pane_id
                    : active_pane_manager().pane_name(id);
                push_toast(1, "Pane '" + pane_label + "' exited unexpectedly. Use restart_host to respawn it.");
            }
            return;
        }
        if (!h.is_running())
            dead.push_back(id);
    });
    if (clean_final_remote_exit)
    {
        input_dispatcher_.set_host(nullptr);
        running_ = false;
        return false;
    }
    if (!dead.empty())
    {
        // Clear the input dispatcher's host pointer before destroying panes so
        // set_host() can't call on_focus_lost() on a dangling pointer.
        input_dispatcher_.set_host(nullptr);
    }
    for (LeafId id : dead)
    {
        announced_dead_panes_.erase(active_pane_manager().pane_id(id));
        if (active_pane_manager().host_count() == 1)
        {
            // Last pane in this tab died.
            if (tab_count() <= 1)
            {
                const SpaceId closing_space_id = space_controller_.active_space_id();
                if (space_controller_.count() > 1
                    && space_controller_.close_space(closing_space_id))
                {
                    mark_session_dirty();
                    refresh_app_shell_layout();
                    input_dispatcher_.set_host(active_pane_manager().focused_host());
                    request_frame();
                    return active_pane_manager().host() != nullptr;
                }

                // The final host has exited, so discard the empty saved
                // topology and terminate the application.
                discard_session_state_on_shutdown_ = true;
                delete_session_state(options_.session_id);
                input_dispatcher_.set_host(nullptr);
                space_controller_.shutdown_all();
                render_root_ = RenderNode{};
                running_ = false;
                return false;
            }
            // Close this tab and switch to another.
            int closing = active_tab_id();
            close_tab(closing);
            refresh_app_shell_layout();
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            request_frame();
            return active_pane_manager().host() != nullptr;
        }
        active_pane_manager().close_leaf(id);
    }
    if (!dead.empty())
        mark_session_dirty();
    return remote_session_client_
        || active_pane_manager().host() != nullptr;
}

void App::rebuild_render_tree()
{
    update_host_presentation_visibility();
    render_root_ = RenderNode{};
    render_root_.tag = "root";

    const auto& hm = active_pane_manager();
    const bool zoomed = hm.is_zoomed();

    // Chrome host draws pane dividers / tab bar — hidden when zoomed.
    if (chrome_host_)
        render_root_.children.push_back({ chrome_host_.get(), !zoomed, "chrome", {} });

    // Active tab's hosts.
    RenderNode ws_node{ nullptr, true, "tab", {} };
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

void App::update_host_presentation_visibility()
{
    const SpaceId active_space_id
        = space_controller_.active_space_id();
    for (const auto& space : space_controller_.spaces())
    {
        const int active_tab_id = space->id == active_space_id
            ? space->tab_controller.active_tab_id()
            : -1;
        for (auto& tab : space->tab_controller.tabs())
        {
            const bool visible = tab->id == active_tab_id;
            tab->pane_manager.for_each_host(
                [visible](LeafId, IHost& host) {
                    host.set_presentation_visible(visible);
                });
        }
    }
}

bool App::render_frame()
{
    PERF_MEASURE();
    // A minimized Vulkan surface is not drawable. In particular, rebuilding
    // or presenting its swapchain can enter unstable platform-driver paths.
    // Preserve the request so the restored window paints immediately.
    if (window_->is_minimized())
        return false;

    // Consume the current request up front so any nested request_frame() calls
    // made during this frame schedule a follow-up frame instead of being
    // cleared at the end of the render.
    frame_requested_ = false;

    update_diagnostics_panel();

    const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
    if (auto* host = active_pane_manager().focused_host())
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

    // A pending print_pane capture is fulfilled by end_frame(); consume it
    // here, BEFORE any other take_captured_frame() poller (run_screenshot's
    // loop) can mistake it for its own. Keep frames flowing until it lands.
    if (print_capture_pending_ && renderer_.capture() != nullptr)
    {
        if (auto captured = renderer_.capture()->take_captured_frame())
        {
            print_capture_pending_ = false;
            finish_print_capture(*captured);
        }
        request_frame();
    }

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
        // One Agents evaluation per frame, shared by the chrome layout, the
        // hit tests, and every control request below.
        agent_controller_.begin_frame();
        process_control_requests();
        flush_pending_remote_split_ratio();
        consume_remote_session_state();

        // Safety net: detect window size changes that SDL may not deliver as
        // events (e.g. during a Windows modal resize drag).
        {
            auto [pw, ph] = window_->size_pixels();
            if (pw != last_pixel_w_ || ph != last_pixel_h_)
                on_resize(pw, ph);
        }
        apply_pending_resize();

        runtime_perf_collector().begin_frame();

        // File-backed sessions never remain alive without a tab.
        if (find_active_tab() == nullptr)
        {
            input_dispatcher_.set_host(nullptr);
            frame_requested_ = false;
            runtime_perf_collector().cancel_frame();
            running_ = false;
            return false;
        }

        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(active_pane_manager().focused_host());

        // Pump all visible hosts via tree walk.
        rebuild_render_tree();
        walk_pump(render_root_);
        pump_background_hosts();
        refresh_tab_default_names();
        // An agent launched by hand into an existing pane (rather than through
        // launch_agent) mutates no pane, tab, or Space, so no existing event
        // re-runs the shell layout. Discovery is what notices it, so drive the
        // rail's visibility off the projection itself. The query is the
        // frame's cached one; per-pane process probes stay rate-limited
        // inside AgentController.
        const bool have_agents = !agent_controller_.frame_agents(space_controller_).empty();
        if (have_agents != last_have_agents_)
        {
            last_have_agents_ = have_agents;
            refresh_app_shell_layout();
            request_frame();
        }
        const auto now = std::chrono::steady_clock::now();
        maybe_checkpoint_session(now);
        refresh_system_resource_snapshot(now);
        if (input_dispatcher_.update(now, config_.chord_timeout_ms))
            request_frame();
        for (auto& warning : text_service_.take_font_warnings())
            push_toast(1, warning);

        // Re-check after pumping (hosts can die during pump).
        if (!close_dead_panes())
        {
            runtime_perf_collector().cancel_frame();
            return false;
        }
        input_dispatcher_.set_host(active_pane_manager().focused_host());

        if (frame_requested_ && !window_->is_minimized())
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

void App::pump_background_hosts()
{
    const SpaceId active_space_id = space_controller_.active_space_id();
    for (const auto& space : space_controller_.spaces())
    {
        const int foreground_tab_id = space->id == active_space_id
            ? space->tab_controller.active_tab_id()
            : -1;
        for (auto& tab : space->tab_controller.tabs())
        {
            if (tab->id == foreground_tab_id)
                continue;
            tab->pane_manager.for_each_host([](LeafId, IHost& host) {
                if (host.is_running())
                    host.pump();
            });
        }
    }
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
    // Some platforms report a zero-sized drawable while minimizing. Keep the
    // last valid layout and let the restore event/safety-net supply its size.
    if (pixel_w <= 0 || pixel_h <= 0)
        return;
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
    refresh_app_shell_layout();
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

    if (const TextServiceConfig text_config = make_text_service_config(config_);
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
    for (const auto& space : space_controller_.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            tab->pane_manager.for_each_host([](LeafId, IHost& host) {
                host.request_close();
            });
        }
    }
    running_ = false;
}

void App::on_window_close_requested()
{
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

bool App::dispatch_to_nvim_host(std::string_view action, bool keep_focus)
{
    // Remember the caller's pane so keep_focus can restore it after we either
    // focus the target Neovim pane or split a new one (both steal focus).
    const LeafId origin_leaf = active_pane_manager().focused_leaf();

    // Find an existing NvimHost via the typed capability query. The first
    // host (in PaneManager iteration order) reporting is_nvim_host()==true wins;
    // this is the same selection policy as before, just without the debug-string
    // heuristic.
    IHost* nvim_host = nullptr;
    LeafId nvim_leaf = kInvalidLeaf;
    active_pane_manager().for_each_host([&nvim_host, &nvim_leaf](LeafId id, IHost& host) {
        if (!nvim_host && host.is_nvim_host())
        {
            nvim_host = &host;
            nvim_leaf = id;
        }
    });

    if (nvim_host)
    {
        nvim_host->dispatch_action(action);
        active_pane_manager().set_focused(keep_focus ? origin_leaf : nvim_leaf);
        if (!keep_focus && nvim_leaf != origin_leaf)
            mark_session_dirty();
        request_frame();
        return true;
    }

    // No existing NvimHost — create a vertical split with one.
    TopologyMutationResult split = mutate_topology({
        .kind = TopologyMutationKind::SplitPane,
        .space_id = space_controller_.active_space_id(),
        .tab_id = active_tab_id(),
        .pane_id = origin_leaf,
        .direction = TopologySplitDirection::Vertical,
        .host_kind = HostKind::Nvim,
    });
    if (!split.accepted())
    {
        push_toast(2, split.error.empty() ? "Failed to spawn nvim host." : split.error);
        return false;
    }
    const LeafId new_leaf = split.pane_id != kInvalidLeaf
        ? split.pane_id
        : active_pane_manager().focused_leaf();

    // split_focused() focuses the new pane; restore the caller's focus when the
    // caller asked to stay put (e.g. Kanban opening a card in the background).
    if (keep_focus && origin_leaf != kInvalidLeaf)
        active_pane_manager().set_focused(origin_leaf);

    refresh_window_layout();
    request_frame();

    IHost* new_host = active_pane_manager().host_for(new_leaf);
    if (new_host)
        new_host->dispatch_action(action);

    return true;
}

bool App::show_markdown_preview(std::string_view path)
{
    PaneManager& hm = active_pane_manager();
    const LeafId owner = hm.focused_leaf();
    if (owner == kInvalidLeaf)
        return false;

    const bool shared = topology_mutation_route_
        && topology_mutation_route_->route_kind()
            == TopologyMutationRouteKind::ServerBacked;
    const bool existed = hm.has_markdown_preview();
    if (shared)
    {
        if (!existed && markdown_preview_split_pending_)
        {
            pending_markdown_preview_path_ = path;
            markdown_preview_close_after_create_ = false;
            return true;
        }
        TopologyMutationResult result;
        if (existed)
        {
            hm.refresh_markdown_preview(path);
            result = mutate_topology({
                .kind = TopologyMutationKind::UpdateClientPane,
                .space_id = space_controller_.active_space_id(),
                .tab_id = active_tab_id(),
                .pane_id = hm.markdown_preview_leaf(),
                .source_path = std::filesystem::path(path),
                .host_kind = HostKind::Markdown,
            });
        }
        else
        {
            result = mutate_topology({
                .kind = TopologyMutationKind::SplitPane,
                .space_id = space_controller_.active_space_id(),
                .tab_id = active_tab_id(),
                .pane_id = owner,
                .source_path = std::filesystem::path(path),
                .direction = TopologySplitDirection::Horizontal,
                .host_kind = HostKind::Markdown,
                .companion_pane = true,
                .ratio = kMarkdownPreviewTopRatio,
            });
        }
        if (!result.accepted())
        {
            push_toast(2, result.error.empty()
                    ? std::string("Failed to open Markdown preview")
                    : result.error);
            return false;
        }
        if (!existed)
        {
            markdown_preview_split_pending_ = true;
            markdown_preview_close_after_create_ = false;
            pending_markdown_preview_path_ = path;
        }
        request_frame();
        return true;
    }

    const LeafId preview = hm.show_markdown_preview(owner, kMarkdownPreviewTopRatio, path, *this);
    if (preview == kInvalidLeaf)
    {
        const std::string& err = hm.error();
        push_toast(2, err.empty() ? std::string("Failed to open Markdown preview") : err);
        return false;
    }

    // Only the first call changes the split layout; refreshing per keystroke as
    // the selection moves would be wasteful (the pane is merely reloaded).
    if (!existed)
    {
        mark_session_dirty();
        refresh_window_layout();
    }
    request_frame();
    return true;
}

void App::hide_markdown_preview()
{
    PaneManager& hm = active_pane_manager();
    if (markdown_preview_split_pending_
        && !hm.has_markdown_preview())
    {
        markdown_preview_close_after_create_ = true;
        return;
    }
    if (!hm.has_markdown_preview())
        return;
    if (topology_mutation_route_
        && topology_mutation_route_->route_kind()
            == TopologyMutationRouteKind::ServerBacked)
    {
        const auto result = mutate_topology({
            .kind = TopologyMutationKind::ClosePane,
            .space_id = space_controller_.active_space_id(),
            .tab_id = active_tab_id(),
            .pane_id = hm.markdown_preview_leaf(),
        });
        if (!result.accepted())
        {
            push_toast(2, result.error.empty()
                    ? std::string("Failed to close Markdown preview")
                    : result.error);
        }
        request_frame();
        return;
    }
    hm.hide_markdown_preview();
    mark_session_dirty();
    refresh_window_layout();
    request_frame();
}

bool App::is_markdown_preview_visible() const
{
    const Space* space = space_controller_.find_active_space();
    if (!space)
        return false;
    const Tab* tab = space->tab_controller.find_active_tab();
    return markdown_preview_split_pending_
        || (tab && tab->pane_manager.has_markdown_preview());
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
    if (options_.server_connection)
    {
        panel.server_connected = true;
        panel.server_pid = options_.server_connection->server_pid;
        panel.server_epoch = options_.server_connection->server_epoch;
        panel.server_build_version = options_.server_connection->build_version;
        panel.server_protocol_major
            = options_.server_connection->protocol_major;
        panel.server_protocol_minor
            = options_.server_connection->protocol_minor;
        panel.server_capabilities = options_.server_connection->capabilities;
    }

    const Tab* tab = find_active_tab();
    if (tab != nullptr && tab->pane_manager.host())
    {
        const HostDebugState host_state = tab->pane_manager.host()->debug_state();
        panel.grid_size = { host_state.grid_cols, host_state.grid_rows };
        panel.dirty_cells = host_state.dirty_cells;
    }

    panel.host_panes.push_back({ "ChromeHost", { 0, 0 }, { last_pixel_w_, last_pixel_h_ } });

    if (tab != nullptr)
    {
        const auto& hm = tab->pane_manager;
        hm.for_each_host([&panel, &hm](LeafId id, IHost& h) {
            const auto dbg = h.debug_state();
            const auto pd = hm.tree().descriptor_for(id);
            panel.host_panes.push_back({ dbg.name, pd.pixel_pos, pd.pixel_size });
        });
    }

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

void App::refresh_app_shell_layout()
{
    if (!window_ || !renderer_.grid())
        return;

    refresh_window_layout();
    const auto [window_width, window_height] = window_->size_pixels();
    const auto [cell_width, cell_height] = renderer_.grid()->cell_size_pixels();
    const int terminal_height = diagnostics_host_
        ? diagnostics_host_->layout().terminal_height
        : window_height;
    const Tab* active_tab = find_active_tab();
    const bool zoomed = active_tab && active_tab->pane_manager.is_zoomed();
    const bool have_agents = !agent_controller_.frame_agents(space_controller_).empty();
    // Keep the pump's transition check in step with whatever the layout just
    // decided, so an unrelated refresh cannot leave the two disagreeing.
    last_have_agents_ = have_agents;
    shell_layout_ = compute_app_shell_layout({
        .window_width = window_width,
        .window_height = window_height,
        .terminal_height = terminal_height,
        .cell_width = cell_width,
        .cell_height = cell_height,
        .preferred_sidebar_columns = config_.space_sidebar_columns,
        .space_count = space_controller_.count(),
        .show_sidebar = space_controller_.count() > 1 || have_agents,
        .show_tab_bar = true,
        .zoomed = zoomed,
    });

    if (chrome_host_)
        chrome_host_->set_shell_layout(shell_layout_);
    for (const auto& space : space_controller_.spaces())
    {
        space->tab_controller.recompute_all_viewports(
            shell_layout_.pane_root.x,
            shell_layout_.pane_root.y,
            std::max(1, shell_layout_.pane_root.w),
            std::max(1, shell_layout_.pane_root.h));
    }
}

bool App::hit_test_app_chrome(int px, int py) const
{
    return contains(shell_layout_.sidebar, px, py)
        || contains(shell_layout_.sidebar_divider, px, py)
        || contains(shell_layout_.tab_bar, px, py);
}

bool App::hit_test_shell_divider(int px, int py) const
{
    return contains(shell_layout_.sidebar_divider, px, py);
}

void App::resize_space_sidebar_to_pixel(int px)
{
    if (!renderer_.grid() || !shell_layout_.sidebar_visible)
        return;
    const auto [cell_width, cell_height] = renderer_.grid()->cell_size_pixels();
    (void)cell_height;
    if (cell_width <= 0)
        return;
    const int relative_x = px - shell_layout_.work_area.x;
    const int columns = (relative_x + cell_width / 2) / cell_width;
    config_.space_sidebar_columns = std::clamp(
        columns, kMinSpaceSidebarColumns, kMaxSpaceSidebarColumns);
    refresh_app_shell_layout();
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

    const bool window_left = desc.pixel_pos.x <= shell_layout_.pane_root.x;
    const bool window_top = desc.pixel_pos.y <= shell_layout_.pane_root.y;
    const bool window_right = desc.pixel_pos.x + desc.pixel_size.x
        >= shell_layout_.pane_root.x + shell_layout_.pane_root.w;
    const bool window_bottom = desc.pixel_pos.y + desc.pixel_size.y
        >= shell_layout_.pane_root.y + shell_layout_.pane_root.h;
    const int requested_left = pane_content_edge_inset(
        config_.focus_border_width, window_left);
    const int requested_top = pane_content_edge_inset(
        config_.focus_border_width, window_top);
    const int requested_right = pane_content_edge_inset(
        config_.focus_border_width, window_right);
    const int requested_bottom = pane_content_edge_inset(
        config_.focus_border_width, window_bottom);
    const int inset_left = std::min(requested_left, std::max(0, viewport.pixel_size.x));
    const int inset_top = std::min(requested_top, std::max(0, viewport.pixel_size.y));
    const int inset_right = std::min(
        requested_right, std::max(0, viewport.pixel_size.x - inset_left));
    const int inset_bottom = std::min(
        requested_bottom, std::max(0, viewport.pixel_size.y - inset_top));
    viewport.pixel_pos.x += inset_left;
    viewport.pixel_pos.y += inset_top;
    viewport.pixel_size.x = std::max(0, viewport.pixel_size.x - inset_left - inset_right);
    viewport.pixel_size.y = std::max(0, viewport.pixel_size.y - inset_top - inset_bottom);

    // Reserve exactly one shared Chrome pill band at the bottom of every pane.
    // ChromeHost fills any fractional grid-row tail with the host background,
    // so it cannot visually merge into this band.
    if (config_.show_pane_status && cell_h > 0)
    {
        const int reserved = std::min(
            viewport.pixel_size.y, chrome_pill_band_height(cell_h));
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
    const SpaceId active_space_id = space_controller_.active_space_id();
    for (const auto& space : space_controller_.spaces())
    {
        const int foreground_tab_id = space->id == active_space_id
            ? space->tab_controller.active_tab_id()
            : -1;
        for (const auto& tab : space->tab_controller.tabs())
        {
            if (tab->id == foreground_tab_id)
                continue;
            tab->pane_manager.for_each_host([&deadline, &any_host_running](LeafId, IHost& host) {
                if (const auto host_deadline = host.next_deadline();
                    host_deadline && (!deadline || *host_deadline < *deadline))
                {
                    deadline = host_deadline;
                }
                any_host_running = any_host_running
                    || host.requires_periodic_wake();
            });
        }
    }
    if (wait_deadline && (!deadline || *wait_deadline < *deadline))
        deadline = wait_deadline;

    if (!deadline)
    {
        if (any_host_running)
            return kHostPollIntervalMs;
        return -1;
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
// Tab orchestration (collection ownership lives in TabController)
// ---------------------------------------------------------------------------

TabController& App::active_tab_controller()
{
    return space_controller_.active_tab_controller();
}

const TabController& App::active_tab_controller() const
{
    return space_controller_.active_tab_controller();
}

PaneManager::Deps App::make_pane_manager_deps(const Space* space)
{
    if (!space)
        space = space_controller_.find_active_space();
    PaneManager::Deps deps;
    deps.options = &options_;
    if (space)
        deps.default_working_dir = space->root_directory.string();
    deps.config = &config_;
    deps.config_document = &config_document_;
    deps.window = window_.get();
    deps.grid_renderer = renderer_.grid();
    deps.imgui_host = renderer_.imgui();
    deps.text_service = &text_service_;
    deps.display_ppi = &display_ppi_;
    deps.owner_lifetime = host_owner_lifetime_;
    deps.allow_local_layout_mutation
        = !options_.enable_remote_topology;
    if (options_.enable_remote_topology)
    {
        deps.request_projected_divider_ratio
            = [this](DividerId divider_id, float ratio) {
                  queue_remote_split_ratio(divider_id, ratio);
              };
    }
    deps.before_host_destroyed = [this](IHost* host) {
        input_dispatcher_.clear_host_if(host);
    };
    deps.compute_viewport = [this](const PaneDescriptor& desc) {
        return viewport_from_descriptor(desc);
    };
    return deps;
}

void App::refresh_tab_default_names()
{
    for (const auto& space : space_controller_.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            if (tab->name_user_set)
                continue;
            IHost* focused = tab->pane_manager.focused_host();
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
            if (tab->name == new_name)
                continue;
            tab->name = std::move(new_name);
            mark_session_dirty();
            request_frame();
        }
    }
}

bool App::can_snapshot_session_state() const
{
    return options_.enable_session_restore
        && space_controller_.all_spaces_restorable();
}

bool App::initialize_remote_topology()
{
    if (options_.server_runtime_directory.empty()
        || options_.server_client_id.empty())
    {
        last_init_error_
            = "Remote topology requires a server runtime and client identity.";
        return false;
    }
    if (find_active_tab() == nullptr)
    {
        TabController& tabs = active_tab_controller();
        const int placeholder
            = tabs.add_projected_tab(
                make_pane_manager_deps());
        if (placeholder < 0
            || !tabs.activate_tab(placeholder))
        {
            last_init_error_
                = "Failed to create the shared Session placeholder.";
            return false;
        }
    }
    remote_session_client_
        = std::make_unique<RemoteSessionClient>(
            RemoteSessionClientOptions{
                .runtime_directory = options_.server_runtime_directory,
                .client_id = options_.server_client_id,
                .session_id = options_.session_id,
                .wake_consumer = [this] { wake_window(); },
                .recovery = options_.client_recovery,
            });
    if (!remote_session_client_->start())
    {
        last_init_error_
            = "Failed to start the shared Session client worker.";
        remote_session_client_.reset();
        return false;
    }
    push_toast(0, "Connecting to shared Session...");
    return true;
}

void App::consume_remote_session_state()
{
    if (!remote_session_client_)
        return;
    auto published
        = remote_session_client_->take_published_state();
    if (!published)
        return;

    for (const auto& warning : published->persistence_warnings)
    {
        push_toast(1,
            "Session persistence: " + warning);
    }

    if (published->server_epoch_changed)
    {
        accept_next_remote_topology_revision_ = true;
        topology_projection_.clear_command_activations();
        markdown_preview_split_pending_ = false;
        markdown_preview_close_after_create_ = false;
        pending_markdown_preview_path_.clear();
        if (published->recovery
            && options_.server_connection)
        {
            options_.server_connection->server_epoch
                = published->recovery->server_epoch;
        }
    }

    if (published->topology_error)
    {
        if (!topology_poll_error_announced_)
        {
            topology_poll_error_announced_ = true;
            push_toast(1, "Shared topology unavailable: " + *published->topology_error);
        }
    }
    else if (published->topology)
    {
        topology_poll_error_announced_ = false;
    }
    if (published->agent_error)
    {
        if (!agent_poll_error_announced_)
        {
            agent_poll_error_announced_ = true;
            push_toast(1, "Shared agents unavailable: " + *published->agent_error);
        }
    }
    else if (published->agents)
    {
        agent_poll_error_announced_ = false;
    }

    for (auto& completion : published->commands)
    {
        if (!completion.ok || !completion.snapshot)
        {
            if (completion.command.kind
                    == TopologyCommandKind::SplitPane
                && !completion.command
                        .companion_owner_pane_id.empty())
            {
                markdown_preview_split_pending_ = false;
                markdown_preview_close_after_create_ = false;
                pending_markdown_preview_path_.clear();
            }
            if (!topology_command_error_announced_)
            {
                topology_command_error_announced_ = true;
                push_toast(2,
                    completion.error_message.empty()
                        ? "Shared topology command failed."
                        : completion.error_message);
            }
            continue;
        }
        topology_command_error_announced_ = false;
        topology_projection_.remember_command_activation(
            std::move(completion.command),
            std::move(completion.created_id),
            completion.snapshot->revision);
    }

    std::string error;
    bool topology_apply_failed = false;
    if (published->topology
        && (accept_next_remote_topology_revision_
            || published->topology->revision
                > remote_topology_snapshot_.revision))
    {
        if (!apply_remote_topology_spaces(
                *published->topology, &error))
        {
            topology_apply_failed = true;
            const std::string apply_error = error.empty()
                ? "Could not project the server topology."
                : error;
            if (announce_remote_topology_apply_error(
                    apply_error))
            {
                push_toast(2,
                    "Could not apply shared topology: "
                        + apply_error);
            }
        }
        else
        {
            topology_projection_.clear_apply_error();
            accept_next_remote_topology_revision_ = false;
            remote_session_client_->acknowledge_topology(
                published->topology_server_epoch,
                published->topology->revision);
        }
    }
    else if (published->topology)
    {
        remote_session_client_->acknowledge_topology(
            published->topology_server_epoch,
            published->topology->revision);
    }

    if (!topology_apply_failed)
    {
        for (auto& pending : topology_projection_
                                 .take_ready_command_activations(
                                     remote_topology_snapshot_.revision))
        {
            apply_remote_command_activation(
                pending.command,
                pending.created_id);
        }
    }

    error.clear();
    if (published->agents && !topology_apply_failed)
    {
        if (!apply_remote_agents(*published->agents, &error))
        {
            push_toast(2,
                "Could not apply shared agents: " + error);
        }
        else
        {
            remote_session_client_->acknowledge_agents(
                published->agent_server_epoch,
                published->agents->revision);
        }
    }

    for (auto& completion : published->statuses)
        handle_remote_status_completion(
            std::move(completion));
}

bool App::announce_remote_topology_apply_error(
    std::string_view error)
{
    return topology_projection_.announce_apply_error(error);
}

bool App::apply_remote_agents(
    const ServerAgentSnapshot& snapshot, std::string* error)
{
    if (snapshot.session_id
        != (options_.session_id.empty()
                ? "default"
                : options_.session_id))
    {
        if (error)
            *error = "Server agent projection targets another Session.";
        return false;
    }

    std::vector<AgentProjection> projected;
    projected.reserve(snapshot.agents.size());
    const auto now = std::chrono::steady_clock::now();
    for (const auto& remote : snapshot.agents)
    {
        const auto space_mapping
            = topology_projection_.local_space(remote.space_id);
        const auto tab_mapping
            = topology_projection_.local_tab(remote.tab_id);
        const auto pane_mapping
            = topology_projection_.local_pane(remote.pane_id);
        if (!space_mapping || !tab_mapping || !pane_mapping
            || tab_mapping->first != *space_mapping)
        {
            continue;
        }
        projected.push_back({
            .space_id = *space_mapping,
            .tab_id = tab_mapping->second,
            .leaf_id = *pane_mapping,
            .pane_id = remote.pane_id,
            .identity = remote.identity,
            .identity_evidence_category
            = remote.identity_evidence_category,
            .identity_high_confidence
            = remote.identity_high_confidence,
            .session_ref = remote.session_ref,
            .lifecycle = remote.lifecycle,
            .generation = remote.generation,
            .runtime_started_at = {},
            .lifecycle_transition_at = now,
            .exit_code = remote.exit_code,
            .status = remote.status,
            .status_authority = remote.status_authority,
            .status_explanation = {
                .status = remote.status,
                .authority = remote.status_authority,
                .manifest_id = remote.manifest_id,
                .manifest_version
                = remote.manifest_version,
                .rule_id = remote.rule_id,
                .evidence_category
                = remote.status_evidence_category,
                .fallback_reason = remote.fallback_reason,
                .observation_generation
                = remote.observation_generation,
                .evaluated_at = now,
            },
            .attention = remote.attention,
            .last_status_transition_at = now,
            .running = remote.running,
            .focused = false,
        });
    }
    agent_controller_.set_server_agents(
        std::move(projected));
    if (error)
        error->clear();
    request_frame();
    return true;
}

bool App::apply_remote_topology_spaces(
    const TopologySnapshot& snapshot, std::string* error)
{
    ScopeExit restore_input([this] {
        input_dispatcher_.set_host(find_active_tab()
                ? active_pane_manager().focused_host()
                : nullptr);
    });
    remote_topology_projection_error_code_.clear();
    if (snapshot.spaces.empty())
    {
        if (error)
            *error = "Server topology contains no Spaces.";
        return false;
    }

    bool structure_changed = false;
    if (topology_projection_.empty())
    {
        const SpaceId local_id = space_controller_.active_space_id();
        topology_projection_.bind_space(
            snapshot.spaces.front().space_id, local_id);
    }

    for (const auto& remote : snapshot.spaces)
    {
        if (topology_projection_.local_space(remote.space_id))
            continue;

        const SpaceId local_id = space_controller_.create_space(
            remote.name, remote.root_directory);
        Space* local = space_controller_.find_space(local_id);
        if (!local)
        {
            if (error)
                *error = "Could not create a projected Space.";
            return false;
        }

        topology_projection_.bind_space(
            remote.space_id, local_id);
        structure_changed = true;
    }

    std::unordered_set<std::string> live_remote_ids;
    for (const auto& remote : snapshot.spaces)
        live_remote_ids.insert(remote.space_id);
    std::vector<std::string> removed;
    for (const auto& [remote_id, local_id] : topology_projection_.spaces())
    {
        if (live_remote_ids.contains(remote_id))
            continue;
        if (space_controller_.close_space(local_id))
        {
            removed.push_back(remote_id);
            structure_changed = true;
        }
    }
    for (const auto& remote_id : removed)
        topology_projection_.erase_space(remote_id);

    for (const auto& remote : snapshot.spaces)
    {
        const auto mapped
            = topology_projection_.local_space(remote.space_id);
        if (!mapped)
            continue;
        space_controller_.rename_space(*mapped, remote.name);
        space_controller_.set_space_root_directory(
            *mapped, remote.root_directory);
    }

    if (!apply_remote_topology_tabs(snapshot, error))
        return false;
    remote_topology_snapshot_ = snapshot;

    if (structure_changed)
    {
        refresh_app_shell_layout();
        input_dispatcher_.set_host(
            active_pane_manager().focused_host());
    }
    request_frame();
    return true;
}

bool App::apply_remote_topology_tabs(
    const TopologySnapshot& snapshot, std::string* error)
{
    std::unordered_set<std::string> live_tab_ids;
    std::unordered_set<std::string> live_pane_ids;
    for (const TopologySpace& remote_space : snapshot.spaces)
    {
        const auto space_mapping
            = topology_projection_.local_space(
                remote_space.space_id);
        if (!space_mapping
            || !space_controller_.find_space(*space_mapping))
        {
            if (error)
                *error = "Projected Space could not be resolved.";
            return false;
        }
        for (const TopologyTab& remote_tab : remote_space.tabs)
        {
            live_tab_ids.insert(remote_tab.tab_id);
            for (const TopologyPane& pane : remote_tab.panes)
                live_pane_ids.insert(pane.pane_id);
            const auto mapped
                = topology_projection_.local_tab(
                    remote_tab.tab_id);
            if (mapped
                && mapped->first != *space_mapping)
            {
                if (error)
                    *error = "Server tab identity moved between Spaces.";
                return false;
            }
        }
    }

    std::string first_error;
    const auto remember_error = [&](std::string message) {
        if (first_error.empty())
            first_error = std::move(message);
    };
    for (const TopologySpace& remote_space : snapshot.spaces)
    {
        const auto space_mapping
            = topology_projection_.local_space(
                remote_space.space_id);
        if (!space_mapping)
            continue;
        Space* local_space
            = space_controller_.find_space(*space_mapping);
        if (!local_space)
            continue;

        TabController& tabs = local_space->tab_controller;
        const int previously_active = tabs.active_tab_id();
        std::unordered_set<int> reserved_local_tabs;
        std::unordered_set<int> claimed_local_tabs;
        for (const auto& [remote_tab_id, mapping] : topology_projection_.tabs())
        {
            if (mapping.first == local_space->id)
                reserved_local_tabs.insert(mapping.second);
        }

        std::vector<int> ordered_local_tabs;
        ordered_local_tabs.reserve(remote_space.tabs.size());
        for (const TopologyTab& remote_tab : remote_space.tabs)
        {
            auto mapping
                = topology_projection_.local_tab(
                    remote_tab.tab_id);
            if (!mapping)
            {
                int local_tab_id = -1;
                for (const auto& local_tab : tabs.tabs())
                {
                    if (!reserved_local_tabs.contains(local_tab->id))
                    {
                        local_tab_id = local_tab->id;
                        break;
                    }
                }
                if (local_tab_id < 0)
                {
                    local_tab_id = tabs.add_projected_tab(
                        make_pane_manager_deps(local_space));
                    if (local_tab_id < 0)
                    {
                        remember_error(tabs.last_error().empty()
                                ? "Could not create a projected tab."
                                : tabs.last_error());
                        continue;
                    }
                }
                topology_projection_.bind_tab(
                    remote_tab.tab_id,
                    local_space->id, local_tab_id);
                reserved_local_tabs.insert(local_tab_id);
                mapping = topology_projection_.local_tab(
                    remote_tab.tab_id);
            }

            claimed_local_tabs.insert(mapping->second);
            ordered_local_tabs.push_back(mapping->second);
            std::string projection_error;
            if (!project_remote_tab(
                    remote_tab, local_space->id,
                    mapping->second, &projection_error))
            {
                remember_error(projection_error.empty()
                        ? "Could not project the server tab layout."
                        : std::move(projection_error));
            }
        }

        // The server owns the tab collection. Remove any locally restored
        // tabs which were not claimed by the current server snapshot.
        std::vector<int> unclaimed;
        for (const auto& local_tab : tabs.tabs())
        {
            if (!claimed_local_tabs.contains(local_tab->id))
                unclaimed.push_back(local_tab->id);
        }
        for (const int tab_id : unclaimed)
            tabs.close_tab(tab_id);
        if (!tabs.reorder_projected_tabs(ordered_local_tabs))
        {
            remember_error(
                "Could not apply authoritative tab order.");
        }

        if (previously_active >= 0
            && claimed_local_tabs.contains(previously_active))
        {
            tabs.activate_tab(previously_active);
        }
        else if (!remote_space.tabs.empty())
        {
            const auto first = topology_projection_.local_tab(
                remote_space.tabs.front().tab_id);
            if (first)
                tabs.activate_tab(first->second);
        }
    }

    std::vector<std::string> removed_tabs;
    for (const auto& [remote_tab_id, mapping] : topology_projection_.tabs())
    {
        if (live_tab_ids.contains(remote_tab_id))
            continue;
        if (Space* space
            = space_controller_.find_space(mapping.first))
        {
            space->tab_controller.close_tab(mapping.second);
        }
        removed_tabs.push_back(remote_tab_id);
    }
    for (const std::string& remote_tab_id : removed_tabs)
        topology_projection_.erase_tab(remote_tab_id);

    topology_projection_.prune_panes(live_pane_ids);

    refresh_app_shell_layout();
    request_frame();
    if (!first_error.empty())
    {
        if (error)
            *error = std::move(first_error);
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool App::project_remote_tab(const TopologyTab& remote,
    SpaceId local_space_id, int local_tab_id, std::string* error)
{
    Space* local_space = space_controller_.find_space(local_space_id);
    if (!local_space)
    {
        if (error)
            *error = "Projected tab Space could not be resolved.";
        return false;
    }
    Tab* local_tab = nullptr;
    for (auto& candidate : local_space->tab_controller.tabs())
    {
        if (candidate->id == local_tab_id)
        {
            local_tab = candidate.get();
            break;
        }
    }
    if (!local_tab)
    {
        if (error)
            *error = "Projected tab could not be resolved.";
        return false;
    }

    local_tab->name = remote.name;
    local_tab->name_user_set = remote.name_user_set;

    std::string projection_error;
    auto projection = topology_projection_.project_tab(
        remote, local_tab->pane_manager.focused_leaf(),
        PaneManager::platform_default_split_host_kind(),
        projection_error);
    if (!projection)
    {
        if (error)
            *error = std::move(projection_error);
        return false;
    }
    for (const auto& [leaf, name] : projection->pane_names)
        local_tab->pane_manager.set_pane_name(leaf, name);
    if (!projection->requires_reconcile)
        return true;

    refresh_app_shell_layout();
    const int pixel_w = std::max(1, shell_layout_.pane_root.w);
    const int pixel_h = std::max(1, shell_layout_.pane_root.h);
    if (!local_tab->pane_manager.reconcile_projected_layout(
            *this, pixel_w, pixel_h, projection->layout))
    {
        const std::string restore_error
            = local_tab->pane_manager.error();
        remote_topology_projection_error_code_
            = local_tab->pane_manager.error_code();
        if (error)
        {
            *error = restore_error.empty()
                ? "Could not project the server tab layout."
                : restore_error;
        }
        return false;
    }
    topology_projection_.commit_tab(
        remote.tab_id, *projection);
    local_tab->initialized = true;
    return true;
}

bool App::execute_remote_topology_command(
    TopologyCommand command, std::string& error)
{
    if (!remote_session_client_)
    {
        error = "Shared topology is not connected.";
        return false;
    }
    return topology_projection_.enqueue_command(
        *remote_session_client_, std::move(command),
        options_.server_client_id, error);
}

void App::apply_remote_command_activation(
    const TopologyCommand& command,
    std::string_view created_id)
{
    if (created_id.empty())
        return;

    if (command.kind
        == TopologyCommandKind::CreateSpace)
    {
        const auto mapped
            = topology_projection_.local_space(created_id);
        if (mapped)
            activate_space(*mapped);
    }
    else if (command.kind
        == TopologyCommandKind::CreateTab)
    {
        const auto mapped
            = topology_projection_.local_tab(created_id);
        if (mapped)
        {
            activate_space(mapped->first);
            activate_tab(mapped->second);
        }
    }
    else if (command.kind
        == TopologyCommandKind::SplitPane)
    {
        const auto tab
            = topology_projection_.local_tab(command.tab_id);
        const std::string_view activation_pane
            = command.companion_owner_pane_id.empty()
            ? std::string_view(created_id)
            : std::string_view(
                  command.companion_owner_pane_id);
        const auto leaf
            = topology_projection_.local_pane(activation_pane);
        if (tab && leaf)
        {
            activate_space(tab->first);
            activate_tab(tab->second);
            active_pane_manager().set_focused(*leaf);
            input_dispatcher_.set_host(
                active_pane_manager().focused_host());
            request_frame();
        }
        if (!command.companion_owner_pane_id.empty())
        {
            const bool close_after_create
                = markdown_preview_close_after_create_;
            const std::string desired_path
                = pending_markdown_preview_path_;
            markdown_preview_split_pending_ = false;
            markdown_preview_close_after_create_ = false;
            pending_markdown_preview_path_.clear();
            if (close_after_create)
                hide_markdown_preview();
            else if (!desired_path.empty()
                && desired_path
                    != command.client_source_path)
            {
                show_markdown_preview(desired_path);
            }
        }
    }
}

void App::initialize_topology_mutation_route()
{
    ServerTopologyMutationRoute::Deps server_deps;
    server_deps.resolve_space
        = [this](SpaceId space_id) {
              return remote_space_id(space_id);
          };
    server_deps.resolve_tab
        = [this](SpaceId space_id, int tab_id) {
              return remote_tab_id(space_id, tab_id);
          };
    server_deps.resolve_pane
        = [this](SpaceId space_id, int tab_id,
              LeafId leaf) -> std::optional<std::string> {
        const Space* space
            = space_controller_.find_space(space_id);
        if (!space)
            return std::nullopt;
        const auto tab = std::ranges::find(
            space->tab_controller.tabs(), tab_id,
            [](const std::unique_ptr<Tab>& value) {
                return value->id;
            });
        if (tab
            == space->tab_controller.tabs().end())
        {
            return std::nullopt;
        }
        const std::string pane_id
            = (*tab)->pane_manager.pane_id(leaf);
        return pane_id.empty()
            ? std::nullopt
            : std::optional(pane_id);
    };
    server_deps.resolve_divider
        = [this](std::string_view tab_id,
              DividerId divider) {
              return topology_projection_.divider_node(
                  tab_id, divider);
          };
    server_deps.resolve_pane_domain
        = [this](SpaceId space_id, int tab_id,
              LeafId leaf) {
              return projected_pane_domain(
                  space_id, tab_id, leaf);
          };
    server_deps.enqueue
        = [this](TopologyCommand command,
              std::string& error) {
              return execute_remote_topology_command(
                  std::move(command), error);
          };
    server_deps.apply_client_local
        = [this](const TopologyMutation& mutation) {
              return apply_local_topology_mutation(
                  mutation);
          };
    server_deps.platform_default_host_kind
        = PaneManager::platform_default_split_host_kind();

    topology_mutation_route_
        = make_topology_mutation_route(
            remote_session_client_ != nullptr,
            [this](const TopologyMutation& mutation) {
                return apply_local_topology_mutation(
                    mutation);
            },
            std::move(server_deps));
}

TopologyMutationResult App::mutate_topology(
    TopologyMutation mutation)
{
    const TopologyMutationKind kind = mutation.kind;
    TopologyMutationResult result
        = topology_mutation_route_
        ? topology_mutation_route_->mutate(mutation)
        : apply_local_topology_mutation(mutation);
    if (!result.applied_locally())
        return result;

    if (kind != TopologyMutationKind::RestartPane)
        mark_session_dirty();
    switch (kind)
    {
    case TopologyMutationKind::RenameSpace:
    case TopologyMutationKind::RenameTab:
    case TopologyMutationKind::RenamePane:
    case TopologyMutationKind::MoveTab:
    case TopologyMutationKind::UpdateClientPane:
        break;
    default:
        if (window_ && chrome_host_ && diagnostics_host_)
            refresh_app_shell_layout();
        input_dispatcher_.set_host(
            active_pane_manager().focused_host());
        break;
    }
    request_frame();
    return result;
}

TopologyMutationResult
App::apply_local_topology_mutation(
    const TopologyMutation& mutation)
{
    Space* space
        = space_controller_.find_space(mutation.space_id);
    auto find_tab = [&]() -> Tab* {
        if (!space)
            return nullptr;
        const auto found = std::ranges::find(
            space->tab_controller.tabs(), mutation.tab_id,
            [](const std::unique_ptr<Tab>& value) {
                return value->id;
            });
        return found == space->tab_controller.tabs().end()
            ? nullptr
            : found->get();
    };

    switch (mutation.kind)
    {
    case TopologyMutationKind::CreateSpace:
    {
        const SpaceId id = space_controller_.create_space(
            mutation.name, mutation.root_directory);
        if (id == kInvalidSpaceId)
        {
            return TopologyMutationResult::rejected(
                "Unable to create the Space.");
        }
        Space* created = space_controller_.find_space(id);
        if (!created)
        {
            return TopologyMutationResult::rejected(
                "Created Space could not be resolved.");
        }
        refresh_app_shell_layout();
        const int pixel_w = std::max(
            1, shell_layout_.pane_root.w);
        const int pixel_h = std::max(
            1, shell_layout_.pane_root.h);
        if (!created->tab_controller.create_initial_tab(
                *this, pixel_w, pixel_h,
                make_pane_manager_deps(created)))
        {
            const std::string error
                = created->tab_controller.last_error();
            space_controller_.close_space(id);
            refresh_app_shell_layout();
            return TopologyMutationResult::rejected(
                error.empty()
                    ? "Failed to create the first Space tab."
                    : error);
        }
        if (!space_controller_.activate_space(id))
        {
            space_controller_.close_space(id);
            return TopologyMutationResult::rejected(
                "Failed to activate the new Space.");
        }
        auto result = TopologyMutationResult::applied();
        result.space_id = id;
        return result;
    }
    case TopologyMutationKind::RenameSpace:
        if (!space_controller_.rename_space(
                mutation.space_id, mutation.name))
        {
            return TopologyMutationResult::rejected(
                "Space was not found.");
        }
        return TopologyMutationResult::applied();
    case TopologyMutationKind::CloseSpace:
    {
        if (!space)
        {
            return TopologyMutationResult::rejected(
                "Space was not found.");
        }
        const bool closing_active
            = mutation.space_id
            == space_controller_.active_space_id();
        if (closing_active)
            input_dispatcher_.set_host(nullptr);
        if (!space_controller_.close_space(
                mutation.space_id))
        {
            if (closing_active)
            {
                input_dispatcher_.set_host(
                    active_pane_manager().focused_host());
            }
            return TopologyMutationResult::rejected(
                "No populated replacement Space is available.");
        }
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::CreateTab:
    {
        if (!space)
            return TopologyMutationResult::rejected(
                "Active Space could not be resolved.");
        const int id = space->tab_controller.add_tab(
            *this, mutation.pixel_width,
            mutation.pixel_height,
            make_pane_manager_deps(space),
            mutation.host_kind);
        if (id < 0)
        {
            const std::string error
                = space->tab_controller.last_error();
            return TopologyMutationResult::rejected(
                error.empty()
                    ? "Failed to create the tab."
                    : error);
        }
        auto result = TopologyMutationResult::applied();
        result.tab_id = id;
        return result;
    }
    case TopologyMutationKind::RenameTab:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Tab was not found.");
        tab->name = mutation.name;
        tab->name_user_set = true;
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::CloseTab:
        if (!space
            || !space->tab_controller.close_tab(
                mutation.tab_id))
        {
            return TopologyMutationResult::rejected(
                "Tab was not found.");
        }
        return TopologyMutationResult::applied();
    case TopologyMutationKind::MoveTab:
        if (!space)
            return TopologyMutationResult::rejected(
                "Tab was not found.");
        space->tab_controller.move_tab(
            mutation.move_delta);
        return TopologyMutationResult::applied();
    case TopologyMutationKind::SplitPane:
    case TopologyMutationKind::DuplicatePane:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Focused pane could not be resolved.");
        PaneManager& panes = tab->pane_manager;
        const SplitDirection direction
            = mutation.kind
                    == TopologyMutationKind::DuplicatePane
                || mutation.direction
                    == TopologySplitDirection::Vertical
            ? SplitDirection::Vertical
            : SplitDirection::Horizontal;
        LeafId new_leaf = kInvalidLeaf;
        if (mutation.kind
            == TopologyMutationKind::DuplicatePane)
        {
            HostLaunchOptions launch;
            launch.kind
                = PaneManager::
                    platform_default_split_host_kind();
            launch.enable_ligatures
                = config_.enable_ligatures;
            if (IHost* host
                = panes.host_for(mutation.pane_id))
            {
                const std::string cwd
                    = host->current_working_directory();
                if (!cwd.empty())
                    launch.working_dir = cwd;
            }
            new_leaf = panes.split_focused(
                direction, std::move(launch), *this);
        }
        else
        {
            new_leaf = mutation.host_kind
                ? panes.split_focused(
                      direction, *mutation.host_kind,
                      *this)
                : panes.split_focused(
                      direction, *this);
        }
        if (new_leaf == kInvalidLeaf)
        {
            return TopologyMutationResult::rejected(
                panes.error().empty()
                    ? "Failed to spawn split pane."
                    : panes.error());
        }
        auto result = TopologyMutationResult::applied();
        result.pane_id = new_leaf;
        return result;
    }
    case TopologyMutationKind::UpdateClientPane:
    {
        Tab* tab = find_tab();
        if (!tab
            || mutation.pane_id
                != tab->pane_manager.markdown_preview_leaf()
            || !tab->pane_manager.refresh_markdown_preview(
                mutation.source_path.string()))
        {
            return TopologyMutationResult::rejected(
                "Client-local pane could not be updated.");
        }
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::ClosePane:
    {
        Tab* tab = find_tab();
        if (!tab || tab->pane_manager.host_count() <= 1)
        {
            return TopologyMutationResult::rejected(
                "Focused pane cannot be closed.");
        }
        input_dispatcher_.set_host(nullptr);
        tab->pane_manager.close_leaf(mutation.pane_id);
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::RenamePane:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Focused pane could not be resolved.");
        tab->pane_manager.set_pane_name(
            mutation.pane_id, mutation.name);
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::SwapPane:
    {
        Tab* tab = find_tab();
        if (!tab
            || !tab->pane_manager
                    .swap_focused_with_next())
        {
            return TopologyMutationResult::rejected(
                "Focused pane could not be reordered.");
        }
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::RestartPane:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Focused pane could not be resolved.");
        input_dispatcher_.set_host(nullptr);
        if (!tab->pane_manager.restart_leaf(
                mutation.pane_id, *this))
        {
            input_dispatcher_.set_host(
                tab->pane_manager.focused_host());
            return TopologyMutationResult::rejected(
                tab->pane_manager.error().empty()
                    ? "Failed to restart the focused pane."
                    : tab->pane_manager.error());
        }
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::SetSplitRatio:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Split could not be resolved.");
        const auto [cell_w, cell_h]
            = renderer_.grid()->cell_size_pixels();
        tab->pane_manager.nudge_divider(
            mutation.divider_id,
            mutation.ratio_delta, cell_w, cell_h);
        return TopologyMutationResult::applied();
    }
    case TopologyMutationKind::EqualizeSplits:
    {
        Tab* tab = find_tab();
        if (!tab)
            return TopologyMutationResult::rejected(
                "Tab could not be resolved.");
        tab->pane_manager.equalize_splits(*this);
        return TopologyMutationResult::applied();
    }
    }
    return TopologyMutationResult::rejected(
        "Unknown topology mutation.");
}

std::optional<TopologyPaneDomain>
App::projected_pane_domain(
    SpaceId local_space_id, int local_tab_id,
    LeafId local_leaf) const
{
    const auto space_id
        = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, local_tab_id);
    const Space* local_space
        = space_controller_.find_space(local_space_id);
    if (!space_id || !tab_id || !local_space)
        return std::nullopt;
    const auto local_tab = std::ranges::find(
        local_space->tab_controller.tabs(), local_tab_id,
        [](const std::unique_ptr<Tab>& value) {
            return value->id;
        });
    if (local_tab
        == local_space->tab_controller.tabs().end())
    {
        return std::nullopt;
    }
    const std::string pane_id
        = (*local_tab)->pane_manager.pane_id(local_leaf);
    if (pane_id.empty())
        return std::nullopt;

    for (const TopologySpace& remote_space : remote_topology_snapshot_.spaces)
    {
        if (remote_space.space_id != *space_id)
            continue;
        const auto remote_tab = std::ranges::find(
            remote_space.tabs, *tab_id,
            &TopologyTab::tab_id);
        if (remote_tab == remote_space.tabs.end())
            return std::nullopt;
        const auto remote_pane = std::ranges::find(
            remote_tab->panes, pane_id,
            &TopologyPane::pane_id);
        return remote_pane == remote_tab->panes.end()
            ? std::nullopt
            : std::optional(remote_pane->domain);
    }
    return std::nullopt;
}

void App::queue_remote_split_ratio(
    DividerId divider_id, float ratio)
{
    if (!topology_mutation_route_
        || topology_mutation_route_->route_kind()
            != TopologyMutationRouteKind::ServerBacked)
    {
        return;
    }
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const int local_tab_id = active_tab_id();
    const auto space_id
        = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, local_tab_id);
    const auto node_id = tab_id
        ? topology_projection_.divider_node(
              *tab_id, divider_id)
        : std::nullopt;
    if (!space_id || !tab_id || !node_id)
        return;
    pending_topology_ratio_ = PendingTopologyRatio{
        .space_id = *space_id,
        .tab_id = *tab_id,
        .node_id = *node_id,
        .ratio = std::clamp(ratio, 0.1f, 0.9f),
        .commit_after = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(75),
    };
}

void App::flush_pending_remote_split_ratio()
{
    if (!pending_topology_ratio_)
        return;
    if (std::chrono::steady_clock::now()
        < pending_topology_ratio_->commit_after)
    {
        return;
    }
    PendingTopologyRatio pending
        = std::move(*pending_topology_ratio_);
    pending_topology_ratio_.reset();
    std::string error;
    if (!execute_remote_topology_command({
                                             .kind = TopologyCommandKind::SetSplitRatio,
                                             .space_id = pending.space_id,
                                             .tab_id = pending.tab_id,
                                             .node_id = pending.node_id,
                                             .ratio = pending.ratio,
                                         },
            error))
    {
        push_toast(2, error);
    }
}

std::optional<std::string> App::remote_space_id(
    SpaceId local_id) const
{
    return topology_projection_.remote_space(local_id);
}

std::optional<std::string> App::remote_tab_id(
    SpaceId local_space_id, int local_tab_id) const
{
    for (const auto& [remote_id, mapping] : topology_projection_.tabs())
    {
        if (mapping.first == local_space_id
            && mapping.second == local_tab_id)
        {
            return remote_id;
        }
    }
    return std::nullopt;
}

Result<SpaceId, Error> App::create_space(
    std::string_view raw_name, std::filesystem::path root_directory)
{
    const std::string name = trim_session_name(raw_name);
    if (name.empty())
        return Result<SpaceId, Error>::err(Error::invalid_argument("Enter a Space name."));
    if (!window_ || !chrome_host_ || !diagnostics_host_)
        return Result<SpaceId, Error>::err(Error::init("Draxul is not ready to create a Space."));

    if (root_directory.empty())
    {
        if (IHost* host = active_pane_manager().focused_host())
        {
            const std::string cwd = host->current_working_directory();
            if (!cwd.empty())
                root_directory = cwd;
        }
        if (root_directory.empty())
        {
            if (const Space* active = space_controller_.find_active_space())
                root_directory = active->root_directory;
        }
        if (root_directory.empty())
            root_directory = options_.host_working_dir;
    }

    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::CreateSpace,
        .space_id = space_controller_.active_space_id(),
        .name = name,
        .root_directory = std::move(root_directory),
    });
    if (!result.accepted())
    {
        return Result<SpaceId, Error>::err(
            Error::invalid_argument(result.error));
    }
    // Server mutations complete asynchronously. The completion activates the
    // newly projected Space; synchronous callers receive the current stable id
    // as their enqueue-success token.
    return result.space_id != kInvalidSpaceId
        ? result.space_id
        : space_controller_.active_space_id();
}

Result<void, Error> App::activate_space(SpaceId id)
{
    const SpaceId previous = space_controller_.active_space_id();
    if (!space_controller_.find_space(id))
        return Result<void, Error>::err(Error::not_found("Space was not found."));
    if (!space_controller_.activate_space(id))
        return Result<void, Error>::err(Error::invalid_argument("Space has no active tab."));

    if (window_ && chrome_host_ && diagnostics_host_)
        refresh_app_shell_layout();
    input_dispatcher_.set_host(active_pane_manager().focused_host());
    if (space_controller_.active_space_id() != previous)
        mark_session_dirty();
    request_frame();
    return Result<void, Error>::ok();
}

Result<void, Error> App::rename_space(SpaceId id, std::string_view raw_name)
{
    const std::string name = trim_session_name(raw_name);
    if (name.empty())
        return Result<void, Error>::err(Error::invalid_argument("Enter a Space name."));
    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::RenameSpace,
        .space_id = id,
        .name = name,
    });
    if (!result.accepted())
    {
        return Result<void, Error>::err(
            Error::invalid_argument(result.error));
    }
    return Result<void, Error>::ok();
}

Result<void, Error> App::close_space(SpaceId id)
{
    if (!space_controller_.find_space(id))
        return Result<void, Error>::err(Error::not_found("Space was not found."));
    if (space_controller_.count() <= 1)
        return Result<void, Error>::err(Error::invalid_argument("The final Space cannot be closed."));

    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::CloseSpace,
        .space_id = id,
    });
    if (!result.accepted())
    {
        return Result<void, Error>::err(
            Error::invalid_argument(result.error));
    }
    return Result<void, Error>::ok();
}

void App::rebuild_agent_definitions()
{
    agent_definitions_ = AgentDefinitionRegistry{};
    for (const AgentProfileConfig& profile : config_.agent_profiles)
    {
        const auto restore_policy = parse_agent_restore_policy(profile.restore_policy);
        agent_definitions_.register_definition({
            .profile_id = profile.id,
            .kind = profile.kind,
            .display_name = profile.display_name,
            .executable = profile.executable,
            .default_args = profile.args,
            .restore_policy = restore_policy.value_or(
                AgentRestorePolicy::ResumeIfAvailable),
        });
    }
}

Result<std::string, Error> App::launch_agent(AgentLaunchRequest request)
{
    const AgentDefinition* definition = agent_definitions_.find(request.profile_id);
    if (!definition)
        return Result<std::string, Error>::err(
            Error::invalid_argument("Unknown agent profile '" + request.profile_id + "'."));
    if (!window_ || !chrome_host_ || !diagnostics_host_)
        return Result<std::string, Error>::err(
            Error::init("Draxul is not ready to launch an agent."));
    if (remote_session_client_)
    {
        const SpaceId local_space_id
            = space_controller_.active_space_id();
        const int local_tab_id
            = active_tab_controller().active_tab_id();
        const LeafId local_leaf
            = active_pane_manager().focused_leaf();
        const auto space_id
            = remote_space_id(local_space_id);
        const auto tab_id
            = remote_tab_id(local_space_id, local_tab_id);
        std::optional<std::string> pane_id;
        if (space_id && tab_id)
        {
            for (const TopologySpace& space : remote_topology_snapshot_.spaces)
            {
                if (space.space_id != *space_id)
                    continue;
                const auto tab = std::ranges::find(
                    space.tabs, *tab_id,
                    &TopologyTab::tab_id);
                if (tab == space.tabs.end())
                    break;
                for (const TopologyPane& pane : tab->panes)
                {
                    const auto mapped
                        = topology_projection_.local_pane(
                            pane.pane_id);
                    if (mapped && *mapped == local_leaf)
                    {
                        pane_id = pane.pane_id;
                        break;
                    }
                }
                break;
            }
        }
        if (!space_id || !tab_id || !pane_id)
        {
            return Result<std::string, Error>::err(
                Error::invalid_argument(
                    "Focused pane has no shared server route."));
        }

        const uint64_t mutation_id
            = next_server_agent_mutation_id_++;
        nlohmann::json params{
            { "session_id", options_.session_id },
            { "client_id", options_.server_client_id },
            { "request_id",
                options_.server_client_id + ":"
                    + std::to_string(mutation_id) },
            { "profile_id", request.profile_id },
            { "space_id", *space_id },
            { "tab_id", *tab_id },
            { "pane_id", *pane_id },
            { "args", request.additional_args },
        };
        if (options_.client_recovery)
        {
            const auto identity
                = options_.client_recovery->server_identity();
            if (!identity.connection_token.empty())
            {
                params["connection_token"]
                    = identity.connection_token;
            }
        }
        if (!request.working_directory.empty())
        {
            params["cwd"]
                = request.working_directory;
        }
        else if (IHost* host
            = active_pane_manager().focused_host())
        {
            const std::string cwd
                = host->current_working_directory();
            if (!cwd.empty())
                params["cwd"] = cwd;
        }
        const auto send_start
            = [&](std::chrono::milliseconds timeout) {
                  return ControlClient::request(
                      namespaced_control_id(
                          kServerControlId,
                          options_.server_runtime_directory),
                      options_.server_runtime_directory,
                      "agent.start", params,
                      { .timeout = timeout });
              };
        auto started
            = send_start(std::chrono::milliseconds(100));
        if (!started.ok
            && (is_transient_client_error(
                    started.error_code)
                || is_resynchronizing_client_error(
                    started.error_code)))
        {
            if (options_.client_recovery
                && (started.error_code
                        == "invalid_connection_token"
                    || started.error_code
                        == "stale_epoch"))
            {
                std::string refresh_error;
                if (options_.client_recovery
                        ->refresh_server_epoch(
                            options_
                                .server_runtime_directory,
                            options_.server_client_id,
                            refresh_error))
                {
                    const auto identity
                        = options_.client_recovery
                              ->server_identity();
                    if (identity.connection_token.empty())
                        params.erase("connection_token");
                    else
                    {
                        params["connection_token"]
                            = identity.connection_token;
                    }
                }
                else if (!refresh_error.empty())
                {
                    started.error_message
                        = std::move(refresh_error);
                }
            }
            started = send_start(
                std::chrono::milliseconds(500));
        }
        if (!started.ok)
        {
            return Result<std::string, Error>::err(
                Error::init(
                    started.error_message.empty()
                        ? "Server failed to launch the agent."
                        : started.error_message));
        }
        // The shared Session worker observes and applies the new topology and
        // agent projection without blocking this GUI action.
        if (started.result.contains("route")
            && started.result["route"].is_object())
        {
            const std::string started_pane
                = started.result["route"].value(
                    "pane_id", std::string{});
            const auto mapped
                = topology_projection_.local_pane(
                    started_pane);
            if (mapped)
            {
                active_pane_manager().set_focused(
                    *mapped);
                input_dispatcher_.set_host(
                    active_pane_manager().focused_host());
            }
        }
        const std::string instance_id
            = started.result.value(
                "instance_id", std::string{});
        if (instance_id.empty())
        {
            return Result<std::string, Error>::err(
                Error::init(
                    "Server launched an agent without returning its identity."));
        }
        return instance_id;
    }

    HostLaunchOptions launch;
    launch.kind = PaneManager::platform_default_split_host_kind();
    launch.command = definition->executable;
    launch.args = definition->default_args;
    launch.args.insert(launch.args.end(),
        request.additional_args.begin(), request.additional_args.end());
    if (!request.working_directory.empty())
        launch.working_dir = std::move(request.working_directory);
    else if (IHost* host = active_pane_manager().focused_host())
        launch.working_dir = host->current_working_directory();

    std::string instance_id;
    const auto existing_agents = agent_controller_.query(space_controller_);
    do
    {
        instance_id = "agent-" + options_.session_id + "-"
            + std::to_string(next_agent_instance_serial_++);
    } while (std::any_of(existing_agents.begin(), existing_agents.end(),
        [&](const AgentProjection& agent) {
            return agent.identity.instance_id == instance_id;
        }));
    launch.environment = {
        { "DRAXUL_ENV", "1" },
        { "DRAXUL_SESSION_ID", options_.session_id },
        { "DRAXUL_SPACE_ID", std::to_string(space_controller_.active_space_id()) },
        { "DRAXUL_TAB_ID", std::to_string(active_tab_controller().active_tab_id()) },
        { "DRAXUL_AGENT_INSTANCE_ID", instance_id },
    };

    PaneManager& panes = active_pane_manager();
    const LeafId leaf = panes.split_focused(
        SplitDirection::Vertical, std::move(launch), *this);
    if (leaf == kInvalidLeaf)
    {
        const std::string detail = panes.error();
        return Result<std::string, Error>::err(Error::init(detail.empty()
                ? "Failed to create an agent pane."
                : detail));
    }

    panes.set_agent_identity(leaf, {
                                       .profile_id = definition->profile_id,
                                       .kind = definition->kind,
                                       .display_name = definition->display_name,
                                       .instance_id = instance_id,
                                   },
        definition->restore_policy);

    // The new agent must reach the rail on this frame, not on the next
    // projection refresh.
    agent_controller_.invalidate();
    mark_session_dirty();
    refresh_app_shell_layout();
    input_dispatcher_.set_host(panes.focused_host());
    request_frame();
    return instance_id;
}

Result<void, Error> App::restart_agent_runtime(
    const AgentProjection& agent)
{
    if (remote_session_client_)
    {
        const uint64_t mutation_id
            = next_server_agent_mutation_id_++;
        nlohmann::json params{
            { "session_id", options_.session_id },
            { "client_id", options_.server_client_id },
            { "request_id",
                options_.server_client_id + ":"
                    + std::to_string(mutation_id) },
            { "instance_id",
                agent.identity.instance_id },
        };
        if (options_.client_recovery)
        {
            const auto identity
                = options_.client_recovery->server_identity();
            if (!identity.connection_token.empty())
            {
                params["connection_token"]
                    = identity.connection_token;
            }
        }

        const auto send_restart
            = [&](std::chrono::milliseconds timeout) {
                  return ControlClient::request(
                      namespaced_control_id(
                          kServerControlId,
                          options_.server_runtime_directory),
                      options_.server_runtime_directory,
                      "agent.restart", params,
                      { .timeout = timeout });
              };
        auto restarted
            = send_restart(std::chrono::milliseconds(100));
        if (!restarted.ok
            && (is_transient_client_error(
                    restarted.error_code)
                || is_resynchronizing_client_error(
                    restarted.error_code)))
        {
            if (options_.client_recovery
                && (restarted.error_code
                        == "invalid_connection_token"
                    || restarted.error_code
                        == "stale_epoch"))
            {
                std::string refresh_error;
                if (options_.client_recovery
                        ->refresh_server_epoch(
                            options_
                                .server_runtime_directory,
                            options_.server_client_id,
                            refresh_error))
                {
                    const auto identity
                        = options_.client_recovery
                              ->server_identity();
                    if (identity.connection_token.empty())
                        params.erase("connection_token");
                    else
                    {
                        params["connection_token"]
                            = identity.connection_token;
                    }
                }
                else if (!refresh_error.empty())
                {
                    restarted.error_message
                        = std::move(refresh_error);
                }
            }
            restarted = send_restart(
                std::chrono::milliseconds(500));
        }
        if (!restarted.ok)
        {
            return Result<void, Error>::err(
                Error::rpc(
                    restarted.error_message.empty()
                        ? "Server failed to restart the agent."
                        : restarted.error_message));
        }

        const uint64_t generation
            = restarted.result.value(
                "runtime_generation", 0ull);
        if (!agent_controller_.note_server_agent_restart(
                agent.identity.instance_id,
                { generation }))
        {
            return Result<void, Error>::err(
                Error::rpc(
                    "Server returned an invalid agent restart result."));
        }
        request_frame();
        return Result<void, Error>::ok();
    }

    Space* space
        = space_controller_.find_space(agent.space_id);
    if (!space)
    {
        return Result<void, Error>::err(
            Error::not_found(
                "The agent Space no longer exists."));
    }
    const auto tab_it = std::ranges::find_if(
        space->tab_controller.tabs(),
        [&agent](const auto& candidate) {
            return candidate
                && candidate->id == agent.tab_id;
        });
    if (tab_it == space->tab_controller.tabs().end())
    {
        return Result<void, Error>::err(
            Error::not_found(
                "The agent tab no longer exists."));
    }

    Tab& tab = **tab_it;
    const bool owns_input
        = space_controller_.active_space_id()
            == agent.space_id
        && space->tab_controller.active_tab_id()
            == agent.tab_id
        && tab.pane_manager.focused_leaf()
            == agent.leaf_id;
    if (owns_input)
        input_dispatcher_.set_host(nullptr);
    const bool restarted
        = tab.pane_manager.restart_leaf(
            agent.leaf_id, *this);
    if (owns_input)
    {
        input_dispatcher_.set_host(
            tab.pane_manager.focused_host());
    }
    if (!restarted)
    {
        return Result<void, Error>::err(
            Error::init(
                tab.pane_manager.error().empty()
                    ? "Failed to restart agent."
                    : tab.pane_manager.error()));
    }

    agent_controller_.invalidate();
    request_frame();
    return Result<void, Error>::ok();
}

std::optional<SessionSnapshot> App::snapshot_session_state() const
{
    PERF_MEASURE();
    if (!can_snapshot_session_state())
        return std::nullopt;

    SessionSnapshot state;
    state.session_id = options_.session_id;
    state.session_name = session_name_.empty() ? options_.session_id : session_name_;
    state.active_space_id = space_controller_.active_space_id();
    state.next_space_id = space_controller_.next_space_id();
    auto spaces = space_controller_.snapshot_spaces();
    if (!spaces)
        return std::nullopt;
    state.spaces = std::move(*spaces);

    return state;
}

void App::mark_session_dirty()
{
    session_dirty_ = true;
    ++session_dirty_generation_;
    last_session_mutation_time_ = std::chrono::steady_clock::now();
}

bool App::persist_session_state()
{
    PERF_MEASURE();
    if (!options_.enable_session_restore || discard_session_state_on_shutdown_)
        return false;

    const uint64_t captured_generation = session_dirty_generation_;
    auto state = snapshot_session_state();
    if (!state)
        return false;

    std::string error;
    if (!save_session_state(*state, &error))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to save shell session state: %s",
            error.empty() ? "unknown error" : error.c_str());
        return false;
    }
    if (session_dirty_generation_ == captured_generation)
        session_dirty_ = false;
    return true;
}

void App::maybe_checkpoint_session(std::chrono::steady_clock::time_point now)
{
    if (!options_.enable_session_restore || discard_session_state_on_shutdown_)
        return;

    // Saved layouts cover shell panes only, so a single nvim/markdown/kanban/
    // score/megacity pane anywhere disables every checkpoint AND the shutdown
    // save. That is deliberate, but it used to be completely silent: the user
    // simply stopped getting their layout back. Report the transition once —
    // never the steady state, and never on the very first evaluation, so an
    // explicit non-shell launch does not open with a warning.
    const bool blocked = !can_snapshot_session_state();
    if (session_persistence_blocked_ && *session_persistence_blocked_ != blocked)
    {
        if (blocked)
        {
            DRAXUL_LOG_INFO(LogCategory::App,
                "Session checkpointing paused: saved layouts cover shell panes only.");
            push_toast(1, "Session saving paused while a non-shell pane is open.");
        }
        else
        {
            DRAXUL_LOG_INFO(LogCategory::App, "Session checkpointing resumed.");
        }
    }
    session_persistence_blocked_ = blocked;

    if (!session_dirty_ || blocked)
        return;
    // Trailing edge: this waits for the session to go QUIET for the interval
    // rather than firing on a fixed period. There is deliberately no maximum
    // staleness — mutations come from discrete user actions, so a session that
    // never settles for 2s is not a shape we have seen.
    if (last_session_mutation_time_.time_since_epoch().count() != 0
        && now - last_session_mutation_time_ < options_.session_checkpoint_interval)
        return;

    persist_session_state();
}

bool App::restore_session_state(int pixel_w, int pixel_h, const SessionSnapshot& state)
{
    PERF_MEASURE();
    if (state.spaces.empty())
        return false;

    const bool restored = space_controller_.restore_spaces(*this, pixel_w, pixel_h,
        state.spaces, state.active_space_id, state.next_space_id,
        [this](const Space* space) { return make_pane_manager_deps(space); });
    if (restored)
    {
        render_root_ = RenderNode{};
        if (!space_controller_.last_restore_warning().empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Session restore recovered usable topology: %s",
                space_controller_.last_restore_warning().c_str());
        }
    }
    return restored;
}

bool App::create_initial_tab(int pixel_w, int pixel_h)
{
    if (!active_tab_controller().create_initial_tab(
            *this, pixel_w, pixel_h, make_pane_manager_deps()))
    {
        last_init_error_ = active_tab_controller().last_error();
        return false;
    }
    mark_session_dirty();
    return true;
}

int App::add_tab(int pixel_w, int pixel_h, std::optional<HostKind> host_kind)
{
    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::CreateTab,
        .space_id = space_controller_.active_space_id(),
        .name = "Tab",
        .host_kind = host_kind,
        .pixel_width = pixel_w,
        .pixel_height = pixel_h,
    });
    if (!result.accepted())
    {
        last_init_error_ = std::move(result.error);
        return -1;
    }
    // Server mutations complete asynchronously and activate their projected
    // tab on acknowledgement.
    return result.tab_id >= 0
        ? result.tab_id
        : active_tab_id();
}

bool App::close_tab(int tab_id)
{
    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::CloseTab,
        .space_id = space_controller_.active_space_id(),
        .tab_id = tab_id,
    });
    if (!result.accepted())
    {
        last_init_error_ = std::move(result.error);
        return false;
    }
    return true;
}

void App::activate_tab(int tab_id)
{
    const int previous = active_tab_controller().active_tab_id();
    active_tab_controller().activate_tab(tab_id);
    if (active_tab_controller().active_tab_id() != previous)
        mark_session_dirty();
    refresh_app_shell_layout();
}

void App::next_tab()
{
    const int previous = active_tab_controller().active_tab_id();
    active_tab_controller().next_tab();
    if (active_tab_controller().active_tab_id() != previous)
        mark_session_dirty();
    refresh_app_shell_layout();
}

void App::prev_tab()
{
    const int previous = active_tab_controller().active_tab_id();
    active_tab_controller().prev_tab();
    if (active_tab_controller().active_tab_id() != previous)
        mark_session_dirty();
    refresh_app_shell_layout();
}

void App::move_tab(int direction)
{
    TopologyMutationResult result = mutate_topology({
        .kind = TopologyMutationKind::MoveTab,
        .space_id = space_controller_.active_space_id(),
        .tab_id = active_tab_id(),
        .move_delta = direction,
    });
    if (!result.accepted())
    {
        push_toast(2, result.error);
    }
}

void App::activate_tab_by_index(int one_based_index)
{
    const int previous = active_tab_controller().active_tab_id();
    active_tab_controller().activate_tab_by_index(one_based_index);
    if (active_tab_controller().active_tab_id() != previous)
        mark_session_dirty();
    refresh_app_shell_layout();
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
    {
        active_pane_manager().set_focused(target);
        mark_session_dirty();
    }
}

PaneManager& App::active_pane_manager()
{
    return active_tab_controller().active_pane_manager();
}

const PaneManager& App::active_pane_manager() const
{
    return active_tab_controller().active_pane_manager();
}

Tab* App::find_active_tab() noexcept
{
    Space* space = space_controller_.find_active_space();
    return space ? space->tab_controller.find_active_tab() : nullptr;
}

const Tab* App::find_active_tab() const noexcept
{
    const Space* space = space_controller_.find_active_space();
    return space ? space->tab_controller.find_active_tab() : nullptr;
}

Tab& App::require_active_tab(std::string_view context)
{
    return active_tab_controller().require_active_tab(context);
}

const Tab& App::require_active_tab(std::string_view context) const
{
    return active_tab_controller().require_active_tab(context);
}

const SplitTree& App::active_tree() const
{
    return active_tab_controller().active_tree();
}

int App::tab_count() const
{
    return active_tab_controller().count();
}

int App::active_tab_id() const
{
    return active_tab_controller().active_tab_id();
}

void App::process_control_requests()
{
    const auto agents = agent_controller_.query(space_controller_);
    const bool should_show_sidebar = space_controller_.count() > 1 || !agents.empty();
    if (shell_layout_.sidebar_visible != should_show_sidebar)
    {
        refresh_app_shell_layout();
        request_frame();
    }

    if (!control_server_)
        return;
    if (!control_events_)
        control_events_ = std::make_unique<ControlEventJournal>();
    control_events_->observe(space_controller_.active_space_id(), agents);
    control_server_->process_pending(
        [this](const ControlRequest& request) { return handle_control_request(request); });
}

ControlMethodResult App::handle_control_request(const ControlRequest& request)
{
    ControlRequestRouter read_router(
        space_controller_, agent_controller_, options_.session_id);
    auto read_agent = [&](std::string_view instance_id) {
        return read_router.handle(
            { request.id, "agent.get", { { "instance_id", instance_id } } });
    };
    auto find_agent = [&](std::string_view instance_id)
        -> std::optional<AgentProjection> {
        const auto agents = agent_controller_.query(space_controller_);
        const auto it = std::find_if(agents.begin(), agents.end(),
            [instance_id](const AgentProjection& agent) {
                return agent.identity.instance_id == instance_id;
            });
        return it == agents.end() ? std::nullopt : std::optional(*it);
    };

    if (request.method == "pane.report_agent_session")
    {
        const auto required_string = [&](const char* name)
            -> std::optional<std::string> {
            if (!request.params.is_object() || !request.params.contains(name)
                || !request.params[name].is_string()
                || request.params[name].get_ref<const std::string&>().empty())
                return std::nullopt;
            return request.params[name].get<std::string>();
        };
        const auto pane_id = required_string("pane_id");
        const auto instance_id = required_string("agent_instance_id");
        const auto source = required_string("source");
        const auto agent_kind = required_string("agent");
        const auto ref_kind_text = required_string("ref_kind");
        const auto ref_value = required_string("ref_value");
        if (!pane_id || !instance_id || !source || !agent_kind
            || !ref_kind_text || !ref_value
            || !request.params.contains("integration_version")
            || !request.params["integration_version"].is_number_unsigned()
            || !request.params.contains("sequence")
            || !request.params["sequence"].is_number_unsigned())
        {
            return ControlMethodResult::error("invalid_params",
                "pane.report_agent_session requires complete routing, source, "
                "version, sequence, and reference fields.");
        }
        const auto ref_kind = parse_agent_session_ref_kind(*ref_kind_text);
        if (!ref_kind)
            return ControlMethodResult::error(
                "invalid_params", "Unknown native session reference kind.");

        PaneManager* target_panes = nullptr;
        LeafId target_leaf = kInvalidLeaf;
        size_t matching_routes = 0;
        for (const auto& space : space_controller_.spaces())
        {
            for (const auto& tab : space->tab_controller.tabs())
            {
                tab->pane_manager.tree().for_each_leaf(
                    [&](LeafId leaf, const PaneDescriptor&) {
                        const AgentIdentity* candidate = tab->pane_manager.agent_identity(leaf);
                        if (tab->pane_manager.pane_id(leaf) == *pane_id
                            && candidate
                            && candidate->instance_id == *instance_id)
                        {
                            target_panes = &tab->pane_manager;
                            target_leaf = leaf;
                            ++matching_routes;
                        }
                    });
            }
        }
        const AgentIdentity* identity = target_panes
            ? target_panes->agent_identity(target_leaf)
            : nullptr;
        if (matching_routes != 1 || !identity || identity->kind != *agent_kind)
        {
            return ControlMethodResult::error(
                "routing_mismatch", "Agent routing identity does not match the pane.");
        }

        AgentSessionRef session_ref{
            .source = *source,
            .agent_kind = *agent_kind,
            .integration_version = request.params["integration_version"].get<uint32_t>(),
            .sequence = request.params["sequence"].get<uint64_t>(),
            .kind = *ref_kind,
            .value = *ref_value,
        };
        std::string validation_error;
        if (!validate_agent_session_ref(session_ref, &validation_error))
            return ControlMethodResult::error(
                "invalid_session_ref", validation_error);
        for (const auto& space : space_controller_.spaces())
        {
            for (const auto& tab : space->tab_controller.tabs())
            {
                bool duplicate = false;
                tab->pane_manager.tree().for_each_leaf(
                    [&](LeafId leaf, const PaneDescriptor&) {
                        const AgentSessionRef* existing = tab->pane_manager.agent_session_ref(leaf);
                        duplicate = duplicate
                            || (existing && (&tab->pane_manager != target_panes || leaf != target_leaf)
                                && existing->source == session_ref.source
                                && existing->agent_kind == session_ref.agent_kind
                                && existing->kind == session_ref.kind
                                && existing->value == session_ref.value);
                    });
                if (duplicate)
                    return ControlMethodResult::error("duplicate_session_ref",
                        "Native agent session is already owned by another pane.");
            }
        }
        if (!target_panes->set_agent_session_ref(
                target_leaf, std::move(session_ref)))
        {
            return ControlMethodResult::error("stale_report",
                "Native session report is stale or was rejected.");
        }
        mark_session_dirty();
        return read_agent(*instance_id);
    }

    if (request.method == "space.focus")
    {
        if (!request.params.is_object() || !request.params.contains("id")
            || !request.params["id"].is_number_integer())
        {
            return ControlMethodResult::error(
                "invalid_params", "space.focus requires an integer 'id'.");
        }
        const SpaceId id = request.params["id"].get<SpaceId>();
        const auto activated = activate_space(id);
        if (!activated)
            return ControlMethodResult::error("not_found", activated.error().message);
        return ControlMethodResult::success({
            { "space_id", id },
            { "active", true },
        });
    }

    if (request.method == "agent.start")
    {
        if (!request.params.is_object()
            || !request.params.contains("profile_id")
            || !request.params["profile_id"].is_string())
        {
            return ControlMethodResult::error(
                "invalid_params", "agent.start requires a string 'profile_id'.");
        }
        if (request.params.contains("space_id"))
        {
            if (!request.params["space_id"].is_number_integer())
                return ControlMethodResult::error(
                    "invalid_params", "'space_id' must be an integer.");
            const auto activated = activate_space(request.params["space_id"].get<SpaceId>());
            if (!activated)
                return ControlMethodResult::error(
                    "not_found", activated.error().message);
        }
        AgentLaunchRequest launch{
            .profile_id = request.params["profile_id"].get<std::string>(),
        };
        if (request.params.contains("args"))
        {
            if (!request.params["args"].is_array()
                || request.params["args"].size() > 64)
            {
                return ControlMethodResult::error(
                    "invalid_params", "'args' must be an array of at most 64 strings.");
            }
            for (const auto& arg : request.params["args"])
            {
                if (!arg.is_string() || arg.get_ref<const std::string&>().size() > 4096)
                    return ControlMethodResult::error(
                        "invalid_params", "Every agent argument must be a bounded string.");
                launch.additional_args.push_back(arg.get<std::string>());
            }
        }
        if (request.params.contains("cwd"))
        {
            if (!request.params["cwd"].is_string())
                return ControlMethodResult::error(
                    "invalid_params", "'cwd' must be a string.");
            launch.working_directory = request.params["cwd"].get<std::string>();
        }
        const auto started = launch_agent(std::move(launch));
        if (!started)
            return ControlMethodResult::error(
                "start_failed", started.error().message);
        return read_agent(started.value());
    }

    if (request.method == "agent.focus" || request.method == "agent.restart"
        || request.method == "agent.send_text" || request.method == "agent.send_keys"
        || request.method == "agent.wait")
    {
        if (!request.params.is_object()
            || !request.params.contains("instance_id")
            || !request.params["instance_id"].is_string())
        {
            return ControlMethodResult::error(
                "invalid_params", request.method + " requires 'instance_id'.");
        }
        const std::string instance_id = request.params["instance_id"].get<std::string>();
        const auto agent = find_agent(instance_id);
        if (!agent)
            return ControlMethodResult::error("not_found", "Agent not found.");

        if (request.method == "agent.focus")
        {
            if (!agent_controller_.focus(space_controller_, instance_id))
                return ControlMethodResult::error("focus_failed", "Unable to focus agent.");
            refresh_app_shell_layout();
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
            return read_agent(instance_id);
        }

        if (request.method == "agent.restart")
        {
            const auto restarted
                = restart_agent_runtime(*agent);
            if (!restarted)
            {
                return ControlMethodResult::error(
                    "restart_failed",
                    restarted.error().message);
            }
            return read_agent(instance_id);
        }

        Space* space = space_controller_.find_space(agent->space_id);
        Tab* tab = nullptr;
        if (space)
        {
            const auto tab_it = std::find_if(space->tab_controller.tabs().begin(),
                space->tab_controller.tabs().end(),
                [&](const auto& candidate) {
                    return candidate && candidate->id == agent->tab_id;
                });
            if (tab_it != space->tab_controller.tabs().end())
                tab = tab_it->get();
        }
        if (!tab)
            return ControlMethodResult::error(
                "agent_replaced", "The agent pane no longer exists.");
        IHost* host = tab->pane_manager.host_for(agent->leaf_id);

        if (request.method == "agent.wait")
        {
            if (request.params.contains("runtime_generation"))
            {
                if (!request.params["runtime_generation"].is_number_unsigned())
                    return ControlMethodResult::error(
                        "invalid_params", "'runtime_generation' must be unsigned.");
                if (request.params["runtime_generation"].get<uint64_t>()
                    != agent->generation.value)
                {
                    return ControlMethodResult::success({
                        { "complete", true },
                        { "outcome", "agent_replaced" },
                        { "agent", read_agent(instance_id).value },
                    });
                }
            }
            std::vector<std::string> desired;
            if (request.params.contains("until"))
            {
                if (!request.params["until"].is_array())
                    return ControlMethodResult::error(
                        "invalid_params", "'until' must be an array.");
                for (const auto& value : request.params["until"])
                {
                    if (!value.is_string())
                        return ControlMethodResult::error(
                            "invalid_params", "'until' values must be strings.");
                    desired.push_back(value.get<std::string>());
                }
            }
            if (desired.empty())
                desired = { "blocked", "done", "exited", "failed" };
            const auto matches = [&](std::string_view value) {
                return std::find(desired.begin(), desired.end(), value)
                    != desired.end();
            };
            const std::string lifecycle(to_string(agent->lifecycle));
            const std::string status(to_string(agent->status));
            const bool complete = matches(lifecycle) || matches(status);
            return ControlMethodResult::success({
                { "complete", complete },
                { "outcome", complete ? (matches(status) ? status : lifecycle) : "" },
                { "agent", read_agent(instance_id).value },
            });
        }

        if (!host)
            return ControlMethodResult::error(
                "not_running", "The agent pane has no live host.");
        std::string bytes;
        if (request.method == "agent.send_text")
        {
            if (!request.params.contains("text")
                || !request.params["text"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params", "agent.send_text requires string 'text'.");
            }
            bytes = request.params["text"].get<std::string>();
            if (bytes.size() > 64 * 1024)
                return ControlMethodResult::error(
                    "invalid_params", "Agent text exceeds 64 KiB.");
        }
        else
        {
            if (!request.params.contains("keys")
                || !request.params["keys"].is_array()
                || request.params["keys"].size() > 64)
            {
                return ControlMethodResult::error(
                    "invalid_params", "agent.send_keys requires at most 64 keys.");
            }
            std::vector<std::string> keys;
            keys.reserve(request.params["keys"].size());
            for (const auto& value : request.params["keys"])
            {
                if (!value.is_string())
                    return ControlMethodResult::error(
                        "invalid_params", "Every key must be a string.");
                keys.push_back(value.get<std::string>());
            }
            std::string key_error;
            auto encoded = encode_agent_keys(keys, key_error);
            if (!encoded)
                return ControlMethodResult::error(
                    "invalid_params", std::move(key_error));
            bytes = std::move(*encoded);
        }
        if (!host->send_agent_input(bytes))
            return ControlMethodResult::error(
                "input_failed", "The agent host rejected input.");
        return read_agent(instance_id);
    }

    if (request.method == "event.subscribe")
    {
        uint64_t cursor = 0;
        size_t limit = 64;
        if (request.params.contains("cursor"))
        {
            if (!request.params["cursor"].is_number_unsigned())
                return ControlMethodResult::error(
                    "invalid_params", "'cursor' must be unsigned.");
            cursor = request.params["cursor"].get<uint64_t>();
        }
        if (request.params.contains("limit"))
        {
            if (!request.params["limit"].is_number_unsigned())
                return ControlMethodResult::error(
                    "invalid_params", "'limit' must be unsigned.");
            limit = std::clamp<size_t>(
                request.params["limit"].get<size_t>(), 1, 128);
        }
        return ControlMethodResult::success(
            control_events_->read_after(cursor, limit));
    }

    return read_router.handle(request);
}

void App::shutdown()
{
    PERF_MEASURE();
    if (remote_session_client_)
    {
        remote_session_client_->stop();
        remote_session_client_.reset();
    }
    if (control_server_)
    {
        control_server_->stop();
        control_server_.reset();
    }
    control_events_.reset();
    // Revoke every window callback before any captured App subsystem begins
    // teardown. The registration tokens also make already-copied late events
    // inert, while clear_callbacks covers any direct test/custom callbacks.
    input_dispatcher_.disconnect();
    window_lifecycle_connection_.reset();
    if (window_)
        window_->clear_callbacks();
#ifdef __APPLE__
    macos_menu_.reset(); // tear down menu before handler goes away
#endif

    if (!discard_session_state_on_shutdown_)
        persist_session_state();

    space_controller_.shutdown_all();
    render_root_ = RenderNode{};

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
        config_to_save.space_sidebar_columns = config_.space_sidebar_columns;
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

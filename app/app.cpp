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
#include "session_id.h"
#include "session_listing.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <draxul/atlas_upload.h>
#include <draxul/control_plane.h>
#include <draxul/grid_host_base.h>
#include <draxul/log.h>
#include <draxul/pane_print.h>
#include <draxul/perf_timing.h>
#include <draxul/pixel_scale.h>
#include <draxul/render_test_driver.h>
#include <draxul/remote_session_client.h>
#include <draxul/sdl_window.h>
#include <draxul/server_client.h>
#include <filesystem>
#include <imgui.h>
#include <sstream>
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

bool is_remote_server_shell_kind(HostKind kind)
{
    switch (kind)
    {
    case HostKind::PowerShell:
    case HostKind::Bash:
    case HostKind::Zsh:
    case HostKind::Wsl:
    case HostKind::RemoteTerminal:
        return true;
    default:
        return false;
    }
}

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
        if (remote_session_client_)
        {
            const auto space_id = remote_space_id(
                space_controller_.active_space_id());
            const auto remote_id = remote_tab_id(
                space_controller_.active_space_id(), tab_id);
            std::string error;
            if (!space_id || !remote_id
                || !execute_remote_topology_command({
                        .kind = TopologyCommandKind::RenameTab,
                        .space_id = space_id.value_or(""),
                        .tab_id = remote_id.value_or(""),
                        .name = std::move(name),
                    },
                    error))
            {
                push_toast(2, error.empty()
                        ? "Could not rename the shared tab."
                        : error);
            }
            return;
        }
        for (auto& tab : active_tab_controller().tabs())
        {
            if (tab->id == tab_id)
            {
                tab->name = std::move(name);
                tab->name_user_set = true;
                mark_session_dirty();
                break;
            }
        }
        request_frame();
    };
    chrome_deps.set_pane_name = [this](LeafId leaf, std::string name) {
        // Apply to whichever tab currently owns the leaf — pane edits
        // are always against the active tab.
        if (remote_session_client_)
        {
            const auto space_id = remote_space_id(
                space_controller_.active_space_id());
            const auto tab_id = remote_tab_id(
                space_controller_.active_space_id(),
                active_tab_id());
            const std::string pane_id
                = active_pane_manager().pane_id(leaf);
            std::string error;
            if (!space_id || !tab_id || pane_id.empty()
                || !execute_remote_topology_command({
                        .kind = TopologyCommandKind::RenamePane,
                        .space_id = space_id.value_or(""),
                        .tab_id = tab_id.value_or(""),
                        .pane_id = pane_id,
                        .name = std::move(name),
                    },
                    error))
            {
                push_toast(2, error.empty()
                        ? "Could not rename the shared pane."
                        : error);
            }
            return;
        }
        active_pane_manager().set_pane_name(leaf, std::move(name));
        mark_session_dirty();
        request_frame();
    };
    chrome_deps.get_pane_name = [this](LeafId leaf) {
        return active_pane_manager().pane_name(leaf);
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
        if (remote_session_client_)
        {
            std::string error;
            if (!split_remote_focused(
                    TopologySplitDirection::Vertical, kind, error))
            {
                push_toast(2, error);
            }
            return;
        }
        LeafId new_leaf = kind
            ? active_pane_manager().split_focused(SplitDirection::Vertical, *kind, *this)
            : active_pane_manager().split_focused(SplitDirection::Vertical, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
        else
        {
            const std::string& err = active_pane_manager().error();
            push_toast(2, err.empty() ? std::string("Failed to spawn split pane") : err);
        }
    };
    gui_deps.on_split_horizontal = [this](std::optional<HostKind> kind) {
        if (remote_session_client_)
        {
            std::string error;
            if (!split_remote_focused(
                    TopologySplitDirection::Horizontal, kind, error))
            {
                push_toast(2, error);
            }
            return;
        }
        LeafId new_leaf = kind
            ? active_pane_manager().split_focused(SplitDirection::Horizontal, *kind, *this)
            : active_pane_manager().split_focused(SplitDirection::Horizontal, *this);
        if (new_leaf != kInvalidLeaf)
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
        else
        {
            const std::string& err = active_pane_manager().error();
            push_toast(2, err.empty() ? std::string("Failed to spawn split pane") : err);
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
    gui_deps.on_save_session_as = [this]() {
        open_save_session_prompt();
    };
    gui_deps.on_load_session = [this]() {
        open_load_session_picker();
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
        PaneManager& panes = active_pane_manager();
        if (!panes.agent_identity(panes.focused_leaf()))
        {
            push_toast(2, "The focused pane does not contain a tracked agent.");
            return;
        }
        input_dispatcher_.set_host(nullptr);
        if (!panes.restart_focused(*this))
            push_toast(2, panes.error().empty() ? "Failed to restart agent." : panes.error());
        input_dispatcher_.set_host(panes.focused_host());
        mark_session_dirty();
        request_frame();
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
        if (remote_session_client_)
        {
            if (active_pane_manager().host_count() > 1)
            {
                std::string error;
                if (!close_remote_focused_pane(error))
                    push_toast(2, error);
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
            else
            {
                // The final shared pane belongs to the server. Closing the
                // client detaches from it instead of deleting server state.
                running_ = false;
            }
            refresh_app_shell_layout();
            if (running_)
            {
                input_dispatcher_.set_host(
                    active_pane_manager().focused_host());
            }
            request_frame();
            return;
        }
        if (active_pane_manager().host_count() <= 1)
        {
            if (tab_count() <= 1)
            {
                input_dispatcher_.set_host(nullptr);
                const SpaceId closing_space_id = space_controller_.active_space_id();
                if (space_controller_.count() > 1
                    && space_controller_.close_space(closing_space_id))
                {
                    mark_session_dirty();
                    refresh_app_shell_layout();
                    input_dispatcher_.set_host(active_pane_manager().focused_host());
                    request_frame();
                    return;
                }

                // Closing the last pane discards this saved topology and exits.
                discard_session_state_on_shutdown_ = true;
                delete_session_state(options_.session_id);
                space_controller_.shutdown_all();
                render_root_ = RenderNode{};
                running_ = false;
                return;
            }
            // Last pane in this tab — close the tab, switch to another.
            input_dispatcher_.set_host(nullptr);
            int closing = active_tab_id();
            close_tab(closing);
            refresh_app_shell_layout();
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            request_frame();
            return;
        }
        input_dispatcher_.set_host(nullptr);
        const bool closing_agent = active_pane_manager().agent_identity(
                                       active_pane_manager().focused_leaf())
            != nullptr;
        active_pane_manager().close_focused();
        mark_session_dirty();
        if (closing_agent)
            refresh_app_shell_layout();
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        request_frame();
    };
    gui_deps.on_restart_host = [this]() {
        if (remote_session_client_)
        {
            const auto server_terminal
                = remote_focused_pane_is_server_terminal();
            if (!server_terminal)
            {
                push_toast(
                    2, "Focused shared pane could not be resolved.");
                return;
            }
            if (*server_terminal)
            {
                std::string error;
                if (!restart_remote_focused_pane(error))
                    push_toast(2, error);
                request_frame();
                return;
            }
        }
        input_dispatcher_.set_host(nullptr);
        if (active_pane_manager().restart_focused(*this))
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            request_frame();
        }
        else
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
        }
    };
    gui_deps.on_swap_pane = [this]() {
        if (remote_session_client_)
        {
            std::string error;
            if (!swap_remote_focused_pane(error))
                push_toast(2, error);
            return;
        }
        if (active_pane_manager().swap_focused_with_next())
        {
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
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
        if (remote_session_client_)
        {
            const auto current = hm.divider_ratio(id);
            if (!current)
                return;
            std::string error;
            if (!set_remote_split_ratio(
                    id, std::clamp(*current + delta, 0.1f, 0.9f),
                    error))
            {
                push_toast(2, error);
            }
            return;
        }
        const auto [cw, ch] = renderer_.grid()->cell_size_pixels();
        hm.nudge_divider(id, delta, cw, ch);
        mark_session_dirty();
        request_frame();
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
        if (remote_session_client_)
        {
            std::string error;
            if (!split_remote_focused(
                    TopologySplitDirection::Vertical,
                    PaneManager::platform_default_split_host_kind(),
                    error))
            {
                push_toast(2, error);
            }
            return;
        }
        if (auto* host = active_pane_manager().focused_host())
        {
            const std::string cwd = host->current_working_directory();
            HostLaunchOptions launch;
            launch.kind = PaneManager::platform_default_split_host_kind();
            launch.enable_ligatures = config_.enable_ligatures;
            if (!cwd.empty())
                launch.working_dir = cwd;
            active_pane_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
            input_dispatcher_.set_host(active_pane_manager().focused_host());
            mark_session_dirty();
            request_frame();
        }
    };
    gui_deps.on_equalize_panes = [this]() {
        if (remote_session_client_)
        {
            std::string error;
            if (!equalize_remote_splits(error))
                push_toast(2, error);
            return;
        }
        active_pane_manager().equalize_splits(*this);
        mark_session_dirty();
        request_frame();
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
    for (const auto& session
        : status.session_statuses)
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
            push_toast(2, error);
            return;
        }
        request_quit();
    };
    if (!palette_host_->open_choices(std::move(request)))
        push_toast(2, "Unable to open server shutdown confirmation.");
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
        push_toast(0, format_server_status_summary(
                          *completion.result.status));
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
    active_pane_manager().for_each_host([this, &dead](LeafId id, const IHost& h) {
        const std::string pane_id = active_pane_manager().pane_id(id);
        if (h.is_running())
        {
            if (!pane_id.empty())
                announced_dead_panes_.erase(pane_id);
            return;
        }
        if (active_pane_manager()
                .is_server_owned_remote_terminal_leaf(id))
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

bool App::render_frame()
{
    PERF_MEASURE();
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
    if (remote_session_client_)
    {
        std::string error;
        if (!split_remote_focused(
                TopologySplitDirection::Vertical,
                HostKind::Nvim, error))
        {
            push_toast(2, error);
            return false;
        }
        const LeafId new_leaf
            = active_pane_manager().focused_leaf();
        if (IHost* new_host
            = active_pane_manager().host_for(new_leaf))
        {
            new_host->dispatch_action(action);
        }
        if (keep_focus && origin_leaf != kInvalidLeaf)
            active_pane_manager().set_focused(origin_leaf);
        input_dispatcher_.set_host(
            active_pane_manager().focused_host());
        request_frame();
        return true;
    }

    LeafId new_leaf = active_pane_manager().split_focused(SplitDirection::Vertical, HostKind::Nvim, *this);
    if (new_leaf == kInvalidLeaf)
    {
        const std::string& err = active_pane_manager().error();
        push_toast(2, err.empty() ? std::string("Failed to spawn nvim host") : err);
        return false;
    }

    // split_focused() focuses the new pane; restore the caller's focus when the
    // caller asked to stay put (e.g. Kanban opening a card in the background).
    if (keep_focus && origin_leaf != kInvalidLeaf)
        active_pane_manager().set_focused(origin_leaf);

    mark_session_dirty();
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

    const bool existed = hm.has_markdown_preview();
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
    if (!hm.has_markdown_preview())
        return;
    hm.hide_markdown_preview();
    mark_session_dirty();
    refresh_window_layout();
    request_frame();
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
                any_host_running = any_host_running || host.is_running();
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

    if (published->server_epoch_changed)
    {
        accept_next_remote_topology_revision_ = true;
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
            push_toast(1, "Shared topology unavailable: "
                    + *published->topology_error);
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
            push_toast(1, "Shared agents unavailable: "
                    + *published->agent_error);
        }
    }
    else if (published->agents)
    {
        agent_poll_error_announced_ = false;
    }

    for (auto& completion : published->commands)
        apply_remote_command_completion(
            std::move(completion));

    std::string error;
    if (published->topology
        && (accept_next_remote_topology_revision_
            || published->topology->revision
                > remote_topology_snapshot_.revision))
    {
        if (!apply_remote_topology_spaces(
                *published->topology, &error))
        {
            push_toast(2,
                "Could not apply shared topology: " + error);
        }
        else
        {
            accept_next_remote_topology_revision_ = false;
        }
    }
    error.clear();
    if (published->agents
        && !apply_remote_agents(*published->agents, &error))
        push_toast(2, "Could not apply shared agents: " + error);

    for (auto& completion : published->statuses)
        handle_remote_status_completion(
            std::move(completion));
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
            = topology_space_to_local_.find(remote.space_id);
        const auto tab_mapping
            = topology_tab_to_local_.find(remote.tab_id);
        const auto pane_mapping
            = topology_pane_to_leaf_.find(remote.pane_id);
        if (space_mapping == topology_space_to_local_.end()
            || tab_mapping == topology_tab_to_local_.end()
            || pane_mapping == topology_pane_to_leaf_.end()
            || tab_mapping->second.first
                != space_mapping->second)
        {
            continue;
        }
        projected.push_back({
            .space_id = space_mapping->second,
            .tab_id = tab_mapping->second.second,
            .leaf_id = pane_mapping->second,
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
    remote_topology_projection_error_code_.clear();
    if (snapshot.spaces.empty())
    {
        if (error)
            *error = "Server topology contains no Spaces.";
        return false;
    }

    bool structure_changed = false;
    if (topology_space_to_local_.empty())
    {
        const SpaceId local_id = space_controller_.active_space_id();
        topology_space_to_local_[snapshot.spaces.front().space_id]
            = local_id;
        local_space_to_topology_[local_id]
            = snapshot.spaces.front().space_id;
    }

    for (const auto& remote : snapshot.spaces)
    {
        if (topology_space_to_local_.contains(remote.space_id))
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

        topology_space_to_local_[remote.space_id] = local_id;
        local_space_to_topology_[local_id] = remote.space_id;
        structure_changed = true;
    }

    std::unordered_set<std::string> live_remote_ids;
    for (const auto& remote : snapshot.spaces)
        live_remote_ids.insert(remote.space_id);
    std::vector<std::string> removed;
    for (const auto& [remote_id, local_id] : topology_space_to_local_)
    {
        if (live_remote_ids.contains(remote_id))
            continue;
        if (space_controller_.close_space(local_id))
        {
            local_space_to_topology_.erase(local_id);
            removed.push_back(remote_id);
            structure_changed = true;
        }
    }
    for (const auto& remote_id : removed)
        topology_space_to_local_.erase(remote_id);

    for (const auto& remote : snapshot.spaces)
    {
        const auto mapped
            = topology_space_to_local_.find(remote.space_id);
        if (mapped == topology_space_to_local_.end())
            continue;
        space_controller_.rename_space(mapped->second, remote.name);
        space_controller_.set_space_root_directory(
            mapped->second, remote.root_directory);
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
            = topology_space_to_local_.find(remote_space.space_id);
        if (space_mapping == topology_space_to_local_.end())
            continue;
        Space* local_space
            = space_controller_.find_space(space_mapping->second);
        if (!local_space)
        {
            if (error)
                *error = "Projected Space could not be resolved.";
            return false;
        }

        TabController& tabs = local_space->tab_controller;
        const int previously_active = tabs.active_tab_id();
        std::unordered_set<int> reserved_local_tabs;
        std::unordered_set<int> claimed_local_tabs;
        for (const auto& [remote_tab_id, mapping]
            : topology_tab_to_local_)
        {
            if (mapping.first == local_space->id)
                reserved_local_tabs.insert(mapping.second);
        }

        std::vector<int> ordered_local_tabs;
        ordered_local_tabs.reserve(remote_space.tabs.size());
        for (const TopologyTab& remote_tab : remote_space.tabs)
        {
            live_tab_ids.insert(remote_tab.tab_id);
            for (const TopologyPane& pane : remote_tab.panes)
                live_pane_ids.insert(pane.pane_id);

            auto mapping
                = topology_tab_to_local_.find(remote_tab.tab_id);
            if (mapping == topology_tab_to_local_.end())
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
                        if (error)
                        {
                            *error = tabs.last_error().empty()
                                ? "Could not create a projected tab."
                                : tabs.last_error();
                        }
                        return false;
                    }
                }
                topology_tab_to_local_[remote_tab.tab_id]
                    = { local_space->id, local_tab_id };
                reserved_local_tabs.insert(local_tab_id);
                mapping = topology_tab_to_local_.find(
                    remote_tab.tab_id);
            }

            if (mapping->second.first != local_space->id)
            {
                if (error)
                    *error = "Server tab identity moved between Spaces.";
                return false;
            }
            claimed_local_tabs.insert(mapping->second.second);
            if (!project_remote_tab(
                    remote_tab, local_space->id,
                    mapping->second.second, error))
            {
                return false;
            }
            ordered_local_tabs.push_back(mapping->second.second);
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
            if (error)
                *error = "Could not apply authoritative tab order.";
            return false;
        }

        if (previously_active >= 0
            && claimed_local_tabs.contains(previously_active))
        {
            tabs.activate_tab(previously_active);
        }
        else if (!remote_space.tabs.empty())
        {
            const auto first = topology_tab_to_local_.find(
                remote_space.tabs.front().tab_id);
            if (first != topology_tab_to_local_.end())
                tabs.activate_tab(first->second.second);
        }
    }

    std::vector<std::string> removed_tabs;
    for (const auto& [remote_tab_id, mapping]
        : topology_tab_to_local_)
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
    {
        topology_tab_to_local_.erase(remote_tab_id);
        topology_tab_layout_signatures_.erase(remote_tab_id);
        topology_tab_divider_nodes_.erase(remote_tab_id);
    }

    std::erase_if(topology_pane_to_leaf_,
        [&](const auto& item) {
            return !live_pane_ids.contains(item.first);
        });

    refresh_app_shell_layout();
    input_dispatcher_.set_host(
        active_pane_manager().focused_host());
    request_frame();
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
    local_tab->name_user_set = true;

    std::ostringstream signature;
    signature << remote.root_node_id << '|';
    for (const TopologyNode& node : remote.nodes)
    {
        signature << node.node_id << ':' << node.is_leaf << ':'
                  << node.pane_id << ':'
                  << static_cast<int>(node.direction) << ':'
                  << node.ratio << ':' << node.first_node_id << ':'
                  << node.second_node_id << ';';
    }
    signature << '|';
    for (const TopologyPane& pane : remote.panes)
    {
        signature << pane.pane_id << ':'
                  << static_cast<int>(pane.domain) << ':'
                  << pane.terminal_id << ':'
                  << pane.client_host_kind << ';';
    }
    const std::string structural_signature = signature.str();
    const auto previous_signature
        = topology_tab_layout_signatures_.find(remote.tab_id);
    if (previous_signature
        != topology_tab_layout_signatures_.end()
        && previous_signature->second == structural_signature)
    {
        for (const TopologyPane& pane : remote.panes)
        {
            const auto leaf = topology_pane_to_leaf_.find(
                pane.pane_id);
            if (leaf != topology_pane_to_leaf_.end())
                local_tab->pane_manager.set_pane_name(
                    leaf->second, pane.name);
        }
        return true;
    }

    std::unordered_map<std::string, const TopologyNode*> nodes;
    std::unordered_map<std::string, const TopologyPane*> panes;
    for (const TopologyNode& node : remote.nodes)
        nodes.emplace(node.node_id, &node);
    for (const TopologyPane& pane : remote.panes)
        panes.emplace(pane.pane_id, &pane);

    PaneManager::PaneLayoutSnapshot layout;
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> projected_panes;
    std::unordered_map<DividerId, std::string> divider_nodes;
    DividerId next_divider_id = 0;
    LeafId maximum_leaf = kInvalidLeaf;
    std::function<std::unique_ptr<SplitTree::SnapshotNode>(
        const std::string&, size_t)>
        project_node;
    project_node = [&](const std::string& node_id, size_t depth)
        -> std::unique_ptr<SplitTree::SnapshotNode> {
        if (depth > kTopologyMaxPanesPerTab
            || visiting.contains(node_id))
        {
            return nullptr;
        }
        const auto found = nodes.find(node_id);
        if (found == nodes.end())
            return nullptr;
        visiting.insert(node_id);
        const TopologyNode& source = *found->second;
        auto result
            = std::make_unique<SplitTree::SnapshotNode>();
        result->is_leaf = source.is_leaf;
        if (source.is_leaf)
        {
            const auto pane = panes.find(source.pane_id);
            if (pane == panes.end())
                return nullptr;
            auto leaf = topology_pane_to_leaf_.find(
                source.pane_id);
            if (leaf == topology_pane_to_leaf_.end())
            {
                const LeafId allocated
                    = next_topology_leaf_id_++;
                leaf = topology_pane_to_leaf_
                           .emplace(source.pane_id, allocated)
                           .first;
            }
            result->leaf_id = leaf->second;
            maximum_leaf = std::max(maximum_leaf, leaf->second);
            projected_panes.insert(source.pane_id);
        }
        else
        {
            divider_nodes.emplace(next_divider_id++, source.node_id);
            result->direction
                = source.direction
                    == TopologySplitDirection::Vertical
                ? SplitDirection::Vertical
                : SplitDirection::Horizontal;
            result->ratio = source.ratio;
            result->first
                = project_node(source.first_node_id, depth + 1);
            result->second
                = project_node(source.second_node_id, depth + 1);
            if (!result->first || !result->second)
                return nullptr;
        }
        visiting.erase(node_id);
        return result;
    };

    layout.tree.root = project_node(remote.root_node_id, 0);
    if (!layout.tree.root
        || projected_panes.size() != remote.panes.size())
    {
        if (error)
            *error = "Server tab contains an invalid split tree.";
        return false;
    }

    LeafId focused = local_tab->pane_manager.focused_leaf();
    bool focused_survives = false;
    for (const auto& [pane_id, leaf] : topology_pane_to_leaf_)
    {
        if (leaf == focused && projected_panes.contains(pane_id))
        {
            focused_survives = true;
            break;
        }
    }
    if (!focused_survives)
    {
        const auto first_pane = topology_pane_to_leaf_.find(
            remote.panes.front().pane_id);
        focused = first_pane == topology_pane_to_leaf_.end()
            ? kInvalidLeaf
            : first_pane->second;
    }
    layout.tree.focused_id = focused;
    layout.tree.next_leaf_id = std::max(
        maximum_leaf + 1, next_topology_leaf_id_);

    for (const TopologyPane& pane : remote.panes)
    {
        const auto leaf = topology_pane_to_leaf_.find(
            pane.pane_id);
        if (leaf == topology_pane_to_leaf_.end())
        {
            if (error)
                *error = "Server tab pane was not projected.";
            return false;
        }
        HostKind host_kind
            = PaneManager::platform_default_split_host_kind();
        if (pane.domain == TopologyPaneDomain::ServerTerminal)
        {
            host_kind = HostKind::RemoteTerminal;
        }
        else if (pane.client_host_kind != "platform_default")
        {
            const auto parsed
                = parse_host_kind(pane.client_host_kind);
            if (!parsed)
            {
                if (error)
                {
                    *error = "Unsupported projected host kind '"
                        + pane.client_host_kind + "'.";
                }
                return false;
            }
            host_kind = *parsed;
        }
        PaneManager::PaneSnapshot projected{
            .leaf_id = leaf->second,
            .launch = {
                .kind = host_kind,
                .remote_terminal_id = pane.terminal_id,
            },
            .pane_name = pane.name,
            .pane_id = pane.pane_id,
        };
        layout.panes.push_back(std::move(projected));
    }

    refresh_app_shell_layout();
    const int pixel_w = std::max(1, shell_layout_.pane_root.w);
    const int pixel_h = std::max(1, shell_layout_.pane_root.h);
    if (!local_tab->pane_manager.reconcile_projected_layout(
            *this, pixel_w, pixel_h, layout))
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
    topology_tab_layout_signatures_[remote.tab_id]
        = structural_signature;
    topology_tab_divider_nodes_[remote.tab_id]
        = std::move(divider_nodes);
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
    if (command.command_id.empty())
    {
        command.command_id = options_.server_client_id + "-"
            + std::to_string(next_topology_command_serial_++);
    }
    if (!remote_session_client_->enqueue(std::move(command)))
    {
        error = "Shared topology command queue is full.";
        return false;
    }
    error.clear();
    return true;
}

void App::apply_remote_command_completion(
    RemoteTopologyCommandCompletion completion)
{
    if (!completion.ok || !completion.snapshot)
    {
        if (!topology_command_error_announced_)
        {
            topology_command_error_announced_ = true;
            push_toast(2,
                completion.error_message.empty()
                    ? "Shared topology command failed."
                    : completion.error_message);
        }
        return;
    }
    topology_command_error_announced_ = false;

    std::unordered_set<std::string> previous_spaces;
    std::unordered_set<std::string> previous_tabs;
    std::unordered_set<std::string> previous_panes;
    for (const TopologySpace& space
        : remote_topology_snapshot_.spaces)
    {
        previous_spaces.insert(space.space_id);
        if (space.space_id != completion.command.space_id)
            continue;
        for (const TopologyTab& tab : space.tabs)
        {
            previous_tabs.insert(tab.tab_id);
            if (tab.tab_id != completion.command.tab_id)
                continue;
            for (const TopologyPane& pane : tab.panes)
                previous_panes.insert(pane.pane_id);
        }
    }

    std::string error;
    if (!apply_remote_topology_spaces(
            *completion.snapshot, &error))
    {
        if (!topology_command_error_announced_)
        {
            topology_command_error_announced_ = true;
            push_toast(2,
                error.empty()
                    ? "Could not apply shared topology."
                    : error);
        }
        return;
    }

    if (completion.command.kind
        == TopologyCommandKind::CreateSpace)
    {
        for (const TopologySpace& space
            : completion.snapshot->spaces)
        {
            if (previous_spaces.contains(space.space_id))
                continue;
            const auto mapped
                = topology_space_to_local_.find(space.space_id);
            if (mapped != topology_space_to_local_.end())
                activate_space(mapped->second);
            break;
        }
    }
    else if (completion.command.kind
        == TopologyCommandKind::CreateTab)
    {
        for (const TopologySpace& space
            : completion.snapshot->spaces)
        {
            if (space.space_id != completion.command.space_id)
                continue;
            for (const TopologyTab& tab : space.tabs)
            {
                if (previous_tabs.contains(tab.tab_id))
                    continue;
                const auto mapped
                    = topology_tab_to_local_.find(tab.tab_id);
                if (mapped != topology_tab_to_local_.end())
                {
                    activate_space(mapped->second.first);
                    activate_tab(mapped->second.second);
                }
                break;
            }
            break;
        }
    }
    else if (completion.command.kind
        == TopologyCommandKind::SplitPane)
    {
        for (const TopologySpace& space
            : completion.snapshot->spaces)
        {
            if (space.space_id != completion.command.space_id)
                continue;
            for (const TopologyTab& tab : space.tabs)
            {
                if (tab.tab_id != completion.command.tab_id)
                    continue;
                for (const TopologyPane& pane : tab.panes)
                {
                    if (previous_panes.contains(pane.pane_id))
                        continue;
                    const auto leaf
                        = topology_pane_to_leaf_.find(
                            pane.pane_id);
                    if (leaf != topology_pane_to_leaf_.end())
                    {
                        active_pane_manager().set_focused(
                            leaf->second);
                        input_dispatcher_.set_host(
                            active_pane_manager()
                                .focused_host());
                        request_frame();
                    }
                    break;
                }
                break;
            }
            break;
        }
    }
}

bool App::split_remote_focused(
    TopologySplitDirection direction,
    std::optional<HostKind> host_kind, std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    const LeafId focused = active_pane_manager().focused_leaf();
    const std::string pane_id
        = active_pane_manager().pane_id(focused);
    if (!space_id || !tab_id || pane_id.empty())
    {
        error = "Focused shared pane could not be resolved.";
        return false;
    }

    const HostKind kind = host_kind.value_or(
        PaneManager::platform_default_split_host_kind());
    const bool server_terminal
        = !host_kind || is_remote_server_shell_kind(kind);

    return execute_remote_topology_command({
            .kind = TopologyCommandKind::SplitPane,
            .space_id = *space_id,
            .tab_id = *tab_id,
            .pane_id = pane_id,
            .direction = direction,
            .pane_domain = server_terminal
                ? TopologyPaneDomain::ServerTerminal
                : TopologyPaneDomain::ClientLocal,
            .client_host_kind = server_terminal
                ? std::string{}
                : std::string(to_string(kind)),
        },
        error);
}

bool App::close_remote_focused_pane(std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    const std::string pane_id = active_pane_manager().pane_id(
        active_pane_manager().focused_leaf());
    if (!space_id || !tab_id || pane_id.empty())
    {
        error = "Focused shared pane could not be resolved.";
        return false;
    }
    return execute_remote_topology_command({
            .kind = TopologyCommandKind::ClosePane,
            .space_id = *space_id,
            .tab_id = *tab_id,
            .pane_id = pane_id,
        },
        error);
}

bool App::swap_remote_focused_pane(std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    PaneManager& panes = active_pane_manager();
    const LeafId focused = panes.focused_leaf();
    const LeafId target = active_tree().next_leaf_after(focused);
    const std::string pane_id = panes.pane_id(focused);
    const std::string target_pane_id = panes.pane_id(target);
    if (!space_id || !tab_id || pane_id.empty()
        || target_pane_id.empty())
    {
        error = "Shared panes could not be resolved for reordering.";
        return false;
    }
    return execute_remote_topology_command({
            .kind = TopologyCommandKind::SwapPane,
            .space_id = *space_id,
            .tab_id = *tab_id,
            .pane_id = pane_id,
            .target_pane_id = target_pane_id,
        },
        error);
}

bool App::restart_remote_focused_pane(std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    const std::string pane_id = active_pane_manager().pane_id(
        active_pane_manager().focused_leaf());
    if (!space_id || !tab_id || pane_id.empty())
    {
        error = "Focused shared pane could not be resolved.";
        return false;
    }
    return execute_remote_topology_command({
            .kind = TopologyCommandKind::RestartPane,
            .space_id = *space_id,
            .tab_id = *tab_id,
            .pane_id = pane_id,
        },
        error);
}

std::optional<bool>
App::remote_focused_pane_is_server_terminal() const
{
    if (!remote_session_client_)
        return std::nullopt;
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    const std::string pane_id = active_pane_manager().pane_id(
        active_pane_manager().focused_leaf());
    if (!space_id || !tab_id || pane_id.empty())
        return std::nullopt;
    for (const TopologySpace& space
        : remote_topology_snapshot_.spaces)
    {
        if (space.space_id != *space_id)
            continue;
        for (const TopologyTab& tab : space.tabs)
        {
            if (tab.tab_id != *tab_id)
                continue;
            for (const TopologyPane& pane : tab.panes)
            {
                if (pane.pane_id == pane_id)
                {
                    return pane.domain
                        == TopologyPaneDomain::ServerTerminal;
                }
            }
        }
    }
    return std::nullopt;
}

bool App::set_remote_split_ratio(
    DividerId divider_id, float ratio, std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    if (!space_id || !tab_id)
    {
        error = "Shared split could not be resolved.";
        return false;
    }
    const auto tab_nodes
        = topology_tab_divider_nodes_.find(*tab_id);
    if (tab_nodes == topology_tab_divider_nodes_.end())
    {
        error = "Shared split mapping is unavailable.";
        return false;
    }
    const auto node = tab_nodes->second.find(divider_id);
    if (node == tab_nodes->second.end())
    {
        error = "Shared split could not be resolved.";
        return false;
    }
    return execute_remote_topology_command({
            .kind = TopologyCommandKind::SetSplitRatio,
            .space_id = *space_id,
            .tab_id = *tab_id,
            .node_id = node->second,
            .ratio = std::clamp(ratio, 0.1f, 0.9f),
        },
        error);
}

bool App::equalize_remote_splits(std::string& error)
{
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    if (!space_id || !tab_id)
    {
        error = "Shared tab could not be resolved.";
        return false;
    }
    return execute_remote_topology_command({
            .kind = TopologyCommandKind::EqualizeSplits,
            .space_id = *space_id,
            .tab_id = *tab_id,
        },
        error);
}

void App::queue_remote_split_ratio(
    DividerId divider_id, float ratio)
{
    if (!remote_session_client_)
        return;
    const SpaceId local_space_id
        = space_controller_.active_space_id();
    const auto space_id = remote_space_id(local_space_id);
    const auto tab_id = remote_tab_id(
        local_space_id, active_tab_id());
    if (!space_id || !tab_id)
        return;
    const auto tab_nodes
        = topology_tab_divider_nodes_.find(*tab_id);
    if (tab_nodes == topology_tab_divider_nodes_.end())
        return;
    const auto node = tab_nodes->second.find(divider_id);
    if (node == tab_nodes->second.end())
        return;
    pending_topology_ratio_ = PendingTopologyRatio{
        .space_id = *space_id,
        .tab_id = *tab_id,
        .node_id = node->second,
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
            .space_id = std::move(pending.space_id),
            .tab_id = std::move(pending.tab_id),
            .node_id = std::move(pending.node_id),
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
    const auto found = local_space_to_topology_.find(local_id);
    if (found == local_space_to_topology_.end())
        return std::nullopt;
    return found->second;
}

std::optional<std::string> App::remote_tab_id(
    SpaceId local_space_id, int local_tab_id) const
{
    for (const auto& [remote_id, mapping]
        : topology_tab_to_local_)
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

    if (remote_session_client_)
    {
        TopologyCommand command{
            .kind = TopologyCommandKind::CreateSpace,
            .name = name,
            .root_directory = root_directory.string(),
            .pane_domain = TopologyPaneDomain::ServerTerminal,
        };
        std::string error;
        if (!execute_remote_topology_command(
                std::move(command), error))
        {
            return Result<SpaceId, Error>::err(
                Error::invalid_argument(error));
        }
        // The completion activates the newly projected Space. Return the
        // current stable id so synchronous callers can treat enqueue as
        // success without waiting on the server.
        return space_controller_.active_space_id();
    }

    const SpaceId id = space_controller_.create_space(name, std::move(root_directory));
    if (id == kInvalidSpaceId)
        return Result<SpaceId, Error>::err(Error::invalid_argument("Unable to create the Space."));
    Space* space = space_controller_.find_space(id);
    if (!space)
        return Result<SpaceId, Error>::err(Error::init("Created Space could not be resolved."));

    refresh_app_shell_layout();
    const int pixel_w = std::max(1, shell_layout_.pane_root.w);
    const int host_h = std::max(1, shell_layout_.pane_root.h);
    if (!space->tab_controller.create_initial_tab(
            *this, pixel_w, host_h, make_pane_manager_deps(space)))
    {
        const std::string error = space->tab_controller.last_error();
        space_controller_.close_space(id);
        refresh_app_shell_layout();
        return Result<SpaceId, Error>::err(Error::spawn(
            error.empty() ? "Failed to create the first Space tab." : error));
    }

    if (!space_controller_.activate_space(id))
    {
        space_controller_.close_space(id);
        return Result<SpaceId, Error>::err(Error::init("Failed to activate the new Space."));
    }

    refresh_app_shell_layout();
    input_dispatcher_.set_host(active_pane_manager().focused_host());
    mark_session_dirty();
    request_frame();
    return id;
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
    if (remote_session_client_)
    {
        const auto remote_id = remote_space_id(id);
        if (!remote_id)
            return Result<void, Error>::err(
                Error::not_found("Server Space was not found."));
        std::string error;
        if (!execute_remote_topology_command({
                .kind = TopologyCommandKind::RenameSpace,
                .space_id = *remote_id,
                .name = name,
            },
                error))
        {
            return Result<void, Error>::err(
                Error::invalid_argument(error));
        }
        return Result<void, Error>::ok();
    }
    if (!space_controller_.rename_space(id, name))
        return Result<void, Error>::err(Error::not_found("Space was not found."));
    mark_session_dirty();
    request_frame();
    return Result<void, Error>::ok();
}

Result<void, Error> App::close_space(SpaceId id)
{
    const bool closing_active = id == space_controller_.active_space_id();
    if (!space_controller_.find_space(id))
        return Result<void, Error>::err(Error::not_found("Space was not found."));
    if (space_controller_.count() <= 1)
        return Result<void, Error>::err(Error::invalid_argument("The final Space cannot be closed."));

    if (remote_session_client_)
    {
        const auto remote_id = remote_space_id(id);
        if (!remote_id)
            return Result<void, Error>::err(
                Error::not_found("Server Space was not found."));
        std::string error;
        if (!execute_remote_topology_command({
                .kind = TopologyCommandKind::CloseSpace,
                .space_id = *remote_id,
            },
                error))
        {
            return Result<void, Error>::err(
                Error::invalid_argument(error));
        }
        return Result<void, Error>::ok();
    }

    if (closing_active)
        input_dispatcher_.set_host(nullptr);
    if (!space_controller_.close_space(id))
    {
        if (closing_active)
            input_dispatcher_.set_host(active_pane_manager().focused_host());
        return Result<void, Error>::err(
            Error::invalid_argument("No populated replacement Space is available."));
    }

    if (window_ && chrome_host_ && diagnostics_host_)
        refresh_app_shell_layout();
    if (closing_active)
        input_dispatcher_.set_host(active_pane_manager().focused_host());
    mark_session_dirty();
    request_frame();
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
            for (const TopologySpace& space
                : remote_topology_snapshot_.spaces)
            {
                if (space.space_id != *space_id)
                    continue;
                const auto tab = std::ranges::find(
                    space.tabs, *tab_id,
                    &TopologyTab::tab_id);
                if (tab == space.tabs.end())
                    break;
                for (const TopologyPane& pane
                    : tab->panes)
                {
                    const auto mapped
                        = topology_pane_to_leaf_.find(
                            pane.pane_id);
                    if (mapped
                            != topology_pane_to_leaf_.end()
                        && mapped->second == local_leaf)
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

        nlohmann::json params{
            { "session_id", options_.session_id },
            { "client_id", options_.server_client_id },
            { "request_id",
                options_.server_client_id + ":"
                    + std::to_string(
                        next_server_agent_mutation_id_++) },
            { "profile_id", request.profile_id },
            { "space_id", *space_id },
            { "tab_id", *tab_id },
            { "pane_id", *pane_id },
            { "args", request.additional_args },
        };
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
        const auto started = ControlClient::request(
            namespaced_control_id(
                kServerControlId,
                options_.server_runtime_directory),
            options_.server_runtime_directory,
            "agent.start", std::move(params),
            {
                .timeout = std::chrono::milliseconds(100),
            });
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
                = topology_pane_to_leaf_.find(
                    started_pane);
            if (mapped != topology_pane_to_leaf_.end())
            {
                active_pane_manager().set_focused(
                    mapped->second);
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

    std::string load_error;
    auto target_state = load_session_state(target_id, &load_error);
    if (!target_state)
    {
        return Result<void, Error>::err(load_error.empty()
                ? Error::not_found("Saved session '" + target_id + "' was not found.")
                : Error::io(load_error));
    }
    if (target_state->spaces.empty()
        || std::none_of(target_state->spaces.begin(), target_state->spaces.end(),
            [](const SpaceSnapshot& space) { return !space.tabs.empty(); }))
        return Result<void, Error>::err(Error::invalid_argument("Saved session has no tabs."));

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
    const int pw = window_ ? window_->width_pixels() : last_pixel_w_;
    const int host_h = std::max(1, shell_layout_.pane_root.h);
    input_dispatcher_.set_host(nullptr);
    if (!restore_session_state(pw, host_h, *target_state))
    {
        input_dispatcher_.set_host(active_pane_manager().focused_host());
        const std::string detail = space_controller_.last_restore_error();
        return Result<void, Error>::err(Error::io(detail.empty()
                ? "Failed to restore the selected session."
                : "Failed to restore the selected session: " + detail));
    }

    options_.session_id = target_id;
    options_.session_name = target_state->session_name.empty() ? target_id : target_state->session_name;
    session_name_ = options_.session_name;
    refresh_app_shell_layout();
    input_dispatcher_.set_host(active_pane_manager().focused_host());
    session_dirty_ = false;
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

    auto delete_new_files = [&]() {
        std::string delete_error;
        if (!delete_session_state(new_id, &delete_error) && !delete_error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to delete rolled-back session state for %s: %s",
                new_id.c_str(),
                delete_error.c_str());
        }
    };

    auto rollback = [&]() {
        options_.session_id = old_id;
        options_.session_name = old_option_name;
        session_name_ = old_session_name;
        delete_new_files();
    };

    options_.session_id = new_id;
    options_.session_name = display_name;
    session_name_ = display_name;

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

    session_dirty_ = false;
    request_frame();
    return new_id;
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
    if (remote_session_client_)
    {
        const SpaceId local_space_id
            = space_controller_.active_space_id();
        const auto space_id = remote_space_id(local_space_id);
        if (!space_id)
        {
            last_init_error_ = "Active shared Space could not be resolved.";
            return -1;
        }
        const HostKind kind = host_kind.value_or(
            PaneManager::platform_default_split_host_kind());
        const bool server_terminal
            = !host_kind || is_remote_server_shell_kind(kind);

        std::string error;
        if (!execute_remote_topology_command({
                .kind = TopologyCommandKind::CreateTab,
                .space_id = *space_id,
                .name = "Tab",
                .pane_domain = server_terminal
                    ? TopologyPaneDomain::ServerTerminal
                    : TopologyPaneDomain::ClientLocal,
                .client_host_kind = server_terminal
                    ? std::string{}
                    : std::string(to_string(kind)),
            },
                error))
        {
            last_init_error_ = std::move(error);
            return -1;
        }
        // The completion activates the newly projected tab.
        return active_tab_id();
    }

    const int id = active_tab_controller().add_tab(
        *this, pixel_w, pixel_h, make_pane_manager_deps(), host_kind);
    if (id < 0)
        last_init_error_ = active_tab_controller().last_error();
    else
        mark_session_dirty();
    return id;
}

bool App::close_tab(int tab_id)
{
    if (remote_session_client_)
    {
        const SpaceId local_space_id
            = space_controller_.active_space_id();
        const auto space_id = remote_space_id(local_space_id);
        const auto remote_id
            = remote_tab_id(local_space_id, tab_id);
        if (!space_id || !remote_id)
        {
            last_init_error_ = "Shared tab could not be resolved.";
            return false;
        }
        std::string error;
        if (!execute_remote_topology_command({
                .kind = TopologyCommandKind::CloseTab,
                .space_id = *space_id,
                .tab_id = *remote_id,
            },
                error))
        {
            last_init_error_ = std::move(error);
            return false;
        }
        return true;
    }

    if (!active_tab_controller().close_tab(tab_id))
        return false;
    mark_session_dirty();
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
    if (remote_session_client_)
    {
        const SpaceId local_space_id
            = space_controller_.active_space_id();
        const auto space_id = remote_space_id(local_space_id);
        const auto tab_id = remote_tab_id(
            local_space_id, active_tab_id());
        if (!space_id || !tab_id)
        {
            push_toast(2, "Shared tab could not be resolved.");
            return;
        }
        std::string error;
        if (!execute_remote_topology_command({
                .kind = TopologyCommandKind::MoveTab,
                .space_id = *space_id,
                .tab_id = *tab_id,
                .move_delta = direction,
            },
                error))
        {
            push_toast(2, error);
        }
        return;
    }
    active_tab_controller().move_tab(direction);
    mark_session_dirty();
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
    const bool should_show_sidebar =
        space_controller_.count() > 1 || !agents.empty();
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

        if (request.method == "agent.restart")
        {
            if (!tab->pane_manager.restart_leaf(agent->leaf_id, *this))
                return ControlMethodResult::error(
                    "restart_failed", tab->pane_manager.error());
            request_frame();
            return read_agent(instance_id);
        }

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

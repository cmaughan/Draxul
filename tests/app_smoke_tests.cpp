// app_smoke_tests.cpp
//
// End-to-end orchestrator smoke tests: exercises App::initialize() ->
// App::pump_once() x N -> App::shutdown() using FakeWindow, FakeRenderer,
// and a minimal FakeHost stub injected via AppOptions::host_factory.
//
// These tests complement app_pump_tests.cpp (which focuses on failure
// rollback paths) by covering the happy-path lifecycle.

#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include "session_state.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <draxul/session_attach.h>
#include <filesystem>
#include <fstream>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

// ---------------------------------------------------------------------------
// Minimal IHost stub — does nothing but report itself as running.
// This lets App::initialize() succeed through the host step without
// spawning a real process or requiring a pty / pipe.
//
// Built on the shared tests::FakeHost with debug-name "smoke-test". FakeHost
// already implements the pump -> request_frame contract, records calls, and
// returns content_ready once initialize() has run.
// ---------------------------------------------------------------------------
class SmokeTestHost : public tests::FakeHost
{
public:
    SmokeTestHost()
        : FakeHost("smoke-test")
    {
    }

    // Backwards-compat accessor — existing test call sites expect a
    // pump_count() method.
    int pump_count() const
    {
        return pump_calls;
    }
};

// A host that fails to initialize — used by the "host init fails" test case.
class FailingInitHost final : public SmokeTestHost
{
public:
    FailingInitHost()
    {
        fail_initialize = true;
        init_error_message = "deliberate test failure";
    }
};

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

std::filesystem::path canonical_path(const std::filesystem::path& path)
{
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

// Shared pointers to the most recently created fakes — lets tests inspect
// renderer/host state after App takes ownership.
SmokeTestHost* g_last_smoke_host = nullptr;
FakeTermRenderer* g_last_fake_renderer = nullptr;
FakeWindow* g_last_fake_window = nullptr;
class ReloadTrackingHost;
ReloadTrackingHost* g_last_reload_host = nullptr;

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    auto renderer = std::make_unique<FakeTermRenderer>();
    g_last_fake_renderer = renderer.get();
    return RendererBundle{ std::move(renderer) };
}

std::unique_ptr<IHost> make_smoke_host(HostKind /*kind*/)
{
    auto host = std::make_unique<SmokeTestHost>();
    g_last_smoke_host = host.get();
    return host;
}

// Host that requests a frame once during initialize() and then stays quiet.
// Used to verify App::initialize paints the first frame even if later pumps
// produce no work.
class InitFrameOnlyHost final : public tests::FakeHost
{
public:
    InitFrameOnlyHost()
        : FakeHost("init-frame-only")
    {
        // FakeHost::pump() auto-requests a frame; we want only the initial
        // frame coming from initialize() to be visible to the test.
        request_frame_on_pump = false;
    }

    bool initialize(const HostContext& ctx, IHostCallbacks& callbacks) override
    {
        const bool ok = FakeHost::initialize(ctx, callbacks);
        if (ok)
            callbacks.request_frame();
        return ok;
    }

    int pump_count() const
    {
        return pump_calls;
    }
};

class ReloadTrackingHost final : public SmokeTestHost
{
public:
    void on_config_reloaded(const HostReloadConfig& config) override
    {
        ++reload_count_;
        last_config_ = config;
    }

    void on_font_metrics_changed() override
    {
        ++font_metrics_changed_count_;
    }

    void set_imgui_font(const std::string&, float) override
    {
        ++imgui_font_update_count_;
    }

    void reset_tracking()
    {
        reload_count_ = 0;
        font_metrics_changed_count_ = 0;
        imgui_font_update_count_ = 0;
        last_config_ = HostReloadConfig{};
    }

    int reload_count() const
    {
        return reload_count_;
    }

    int font_metrics_changed_count() const
    {
        return font_metrics_changed_count_;
    }

    const HostReloadConfig& last_config() const
    {
        return last_config_;
    }

private:
    int reload_count_ = 0;
    int font_metrics_changed_count_ = 0;
    int imgui_font_update_count_ = 0;
    HostReloadConfig last_config_;
};

namespace
{
std::vector<FakeHost*> g_viewport_hosts;
} // namespace

class ViewportTrackingHost final : public SmokeTestHost
{
public:
    ViewportTrackingHost()
    {
        set_debug_name("viewport-tracking");
    }
};

// Constructs AppOptions for a fully-initializable App with all fakes.
AppOptions make_smoke_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = bundled_font_path();
    opts.window_factory = []() {
        auto window = std::make_unique<FakeWindow>();
        g_last_fake_window = window.get();
        return window;
    };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = &make_smoke_host;
    opts.host_kind = HostKind::Nvim; // value is irrelevant — factory ignores it
    return opts;
}

struct CurrentDirGuard
{
    std::filesystem::path original;

    explicit CurrentDirGuard(std::filesystem::path cwd)
        : original(std::move(cwd))
    {
    }

    ~CurrentDirGuard()
    {
        std::error_code ec;
        std::filesystem::current_path(original, ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Happy path: full lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialize succeeds with all fakes", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());

    REQUIRE(app.initialize());
    REQUIRE(app.init_error().empty());
    REQUIRE(g_last_smoke_host != nullptr);
    REQUIRE(g_last_smoke_host->was_initialized());

    app.shutdown();
}

TEST_CASE("app smoke: initialize does not mutate cwd when using bundled font fallback",
    "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    const auto original_cwd = std::filesystem::current_path();
    auto temp_cwd = std::filesystem::temp_directory_path() / "draxul-cwd-stability";
    std::error_code ec;
    std::filesystem::remove_all(temp_cwd, ec);
    std::filesystem::create_directories(temp_cwd);
    std::filesystem::current_path(temp_cwd);
    CurrentDirGuard guard(original_cwd);

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());
    REQUIRE(canonical_path(std::filesystem::current_path()) == canonical_path(temp_cwd));

    app.shutdown();
    REQUIRE(canonical_path(std::filesystem::current_path()) == canonical_path(temp_cwd));
}

TEST_CASE("app smoke: pump_once runs without crash after successful init", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());

    // Run the smoke test helper which internally calls pump_once in a loop.
    // With a fake host that reports content_ready immediately, the smoke test
    // should complete well before the timeout.
    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(2000));
    REQUIRE(smoke_ok);

    // The host should have been pumped at least once during the smoke test loop.
    REQUIRE(g_last_smoke_host != nullptr);
    REQUIRE(g_last_smoke_host->pump_count() > 0);

    app.shutdown();
}

TEST_CASE("app smoke: queued window resize updates the host viewport", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    g_last_fake_window = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());
    REQUIRE(g_last_smoke_host != nullptr);
    REQUIRE(g_last_fake_window != nullptr);

    const HostViewport before = g_last_smoke_host->last_viewport;
    g_last_fake_window->queue_resize(1200, 900);

    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(200));
    REQUIRE(smoke_ok);

    const HostViewport after = g_last_smoke_host->last_viewport;
    CHECK(after.pixel_size.x > before.pixel_size.x);
    CHECK(after.pixel_size.y > before.pixel_size.y);
    CHECK(after.grid_size.x >= before.grid_size.x);
    CHECK(after.grid_size.y >= before.grid_size.y);

    app.shutdown();
}

TEST_CASE("app smoke: shutdown after successful init is clean", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());

    app.shutdown();
    // Second shutdown must not crash (idempotency).
    app.shutdown();
}

TEST_CASE("app smoke: initial frame renders before any later host redraw", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_fake_renderer = nullptr;
    AppOptions opts = make_smoke_options();
    opts.show_diagnostics_on_startup = true;
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        return std::make_unique<InitFrameOnlyHost>();
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(g_last_fake_renderer != nullptr);

    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));
    REQUIRE(g_last_fake_renderer->render_imgui_calls > 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Failure path: host factory returns nullptr
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialization fails when host factory returns nullptr", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_smoke_options();
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> { return nullptr; };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
    // Destructor runs — must not crash.
}

// ---------------------------------------------------------------------------
// Failure path: host initialize() returns false
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialization fails when host init fails", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_smoke_options();
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        return std::make_unique<FailingInitHost>();
    };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
    // Shutdown after failed init must not crash.
    app.shutdown();
}

// ---------------------------------------------------------------------------
// Host dies mid-pump — App survives gracefully
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: host death during pump_once does not crash the app", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());
    REQUIRE(g_last_smoke_host != nullptr);

    // Simulate host process death.
    g_last_smoke_host->shutdown();

    // A dead host should not crash the app loop. The current remain-on-exit
    // behavior preserves the pane so the app can keep pumping and rendering.
    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(200));
    REQUIRE(smoke_ok);

    app.shutdown();
}

TEST_CASE("app smoke: reload_config action reloads user config from disk", "[app_smoke][config]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-reload-config");
    HomeDirRedirect redir(temp.path);
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 11.0\n"
               "palette_bg_alpha = 0.9\n"
               "smooth_scroll = true\n"
               "scroll_speed = 1.0\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n";
    }

    FakeWindow* created_window = nullptr;
    g_last_reload_host = nullptr;

    AppOptions opts = make_smoke_options();
    opts.load_user_config = true;
    opts.save_user_config = false;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<ReloadTrackingHost>();
        g_last_reload_host = host.get();
        return host;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_reload_host != nullptr);
    g_last_reload_host->reset_tracking();

    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 14.5\n"
               "palette_bg_alpha = 0.4\n"
               "smooth_scroll = false\n"
               "scroll_speed = 2.5\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n";
    }

    REQUIRE(created_window->on_key != nullptr);
    created_window->on_key(KeyEvent{ 0, SDLK_R, kModCtrl | kModAlt, true });

    REQUIRE(g_last_reload_host->reload_count() == 1);
    REQUIRE(g_last_reload_host->font_metrics_changed_count() > 0);
    CHECK(g_last_reload_host->last_config().font_size == Catch::Approx(14.5f));
    CHECK(g_last_reload_host->last_config().palette_bg_alpha == Catch::Approx(0.4f));
    CHECK(g_last_reload_host->last_config().smooth_scroll == false);
    CHECK(g_last_reload_host->last_config().scroll_speed == Catch::Approx(2.5f));

    app.shutdown();
}

// ---------------------------------------------------------------------------
// WI 107 — inactive workspaces also receive config reloads + font updates
// (regression guard for WI 104 config-font-inactive-workspace-bias).
// ---------------------------------------------------------------------------

namespace
{
std::vector<ReloadTrackingHost*> g_all_reload_hosts;
} // namespace

TEST_CASE("app smoke: reload_config propagates to hosts in inactive workspaces",
    "[app_smoke][config][workspaces]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-multi-ws-reload");
    HomeDirRedirect redir(temp.path);
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 11.0\n"
               "palette_bg_alpha = 0.9\n"
               "smooth_scroll = true\n"
               "scroll_speed = 1.0\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n"
               "new_tab = \"Ctrl+T\"\n";
    }

    g_all_reload_hosts.clear();
    FakeWindow* created_window = nullptr;

    AppOptions opts = make_smoke_options();
    opts.load_user_config = true;
    opts.save_user_config = false;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<ReloadTrackingHost>();
        g_all_reload_hosts.push_back(host.get());
        return host;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    // First workspace's host was created during initialize.
    REQUIRE(g_all_reload_hosts.size() == 1);

    // Open a second workspace via the new_tab keybinding (Ctrl+T). The new
    // workspace becomes active, leaving workspace 1's host inactive.
    REQUIRE(created_window->on_key != nullptr);
    created_window->on_key(KeyEvent{ 0, SDLK_T, kModCtrl, true });
    REQUIRE(g_all_reload_hosts.size() == 2);

    ReloadTrackingHost* host_inactive = g_all_reload_hosts[0];
    ReloadTrackingHost* host_active = g_all_reload_hosts[1];

    host_inactive->reset_tracking();
    host_active->reset_tracking();

    // Rewrite the config with a different font size + ligature setting so the
    // reload actually changes something.
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 14.5\n"
               "enable_ligatures = false\n"
               "palette_bg_alpha = 0.4\n"
               "smooth_scroll = false\n"
               "scroll_speed = 2.5\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n"
               "new_tab = \"Ctrl+T\"\n";
    }

    created_window->on_key(KeyEvent{ 0, SDLK_R, kModCtrl | kModAlt, true });

    // Both hosts must see the reload exactly once — no double-apply on the
    // active workspace, and no skipping of the inactive one.
    INFO("inactive host reload count");
    REQUIRE(host_inactive->reload_count() == 1);
    INFO("active host reload count");
    REQUIRE(host_active->reload_count() == 1);

    // Both hosts must observe the new font metrics, since font_size changed.
    REQUIRE(host_inactive->font_metrics_changed_count() >= 1);
    REQUIRE(host_active->font_metrics_changed_count() >= 1);

    // And both must have received the new config payload.
    CHECK(host_inactive->last_config().font_size == Catch::Approx(14.5f));
    CHECK(host_active->last_config().font_size == Catch::Approx(14.5f));
    CHECK(host_inactive->last_config().enable_ligatures == false);
    CHECK(host_active->last_config().enable_ligatures == false);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// WI 66 — config reload propagates to multiple panes within a single workspace
// (the WI 107 case fans out across workspaces; this case fans out across the
//  splits inside one workspace's HostManager).
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: reload_config propagates to all split panes in the active workspace",
    "[app_smoke][config][splits]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-multi-pane-reload");
    HomeDirRedirect redir(temp.path);
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 11.0\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n"
               // Override the default chord with a single-key binding so the
               // test driver doesn't need to drive a tmux-style prefix.
               "split_vertical = \"Ctrl+Alt+V\"\n";
    }

    g_all_reload_hosts.clear();
    FakeWindow* created_window = nullptr;

    AppOptions opts = make_smoke_options();
    opts.load_user_config = true;
    opts.save_user_config = false;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<ReloadTrackingHost>();
        g_all_reload_hosts.push_back(host.get());
        return host;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(g_all_reload_hosts.size() == 1);

    // Trigger a vertical split inside the (only) workspace. This creates a
    // second pane / second host in the same HostManager.
    REQUIRE(created_window->on_key != nullptr);
    created_window->on_key(KeyEvent{ 0, SDLK_V, kModCtrl | kModAlt, true });
    REQUIRE(g_all_reload_hosts.size() == 2);

    ReloadTrackingHost* pane_a = g_all_reload_hosts[0];
    ReloadTrackingHost* pane_b = g_all_reload_hosts[1];

    pane_a->reset_tracking();
    pane_b->reset_tracking();

    // Rewrite the config and trigger reload.
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 16.0\n"
               "enable_ligatures = false\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n"
               "split_vertical = \"Ctrl+Alt+V\"\n";
    }
    created_window->on_key(KeyEvent{ 0, SDLK_R, kModCtrl | kModAlt, true });

    // Both panes in the same workspace must see the reload exactly once
    // (regression guard for "for_each_host fan-out within a HostManager").
    REQUIRE(pane_a->reload_count() == 1);
    REQUIRE(pane_b->reload_count() == 1);
    REQUIRE(pane_a->font_metrics_changed_count() >= 1);
    REQUIRE(pane_b->font_metrics_changed_count() >= 1);
    CHECK(pane_a->last_config().font_size == Catch::Approx(16.0f));
    CHECK(pane_b->last_config().font_size == Catch::Approx(16.0f));
    CHECK(pane_a->last_config().enable_ligatures == false);
    CHECK(pane_b->last_config().enable_ligatures == false);

    app.shutdown();
}

TEST_CASE("app smoke: save_session_as persists a named session and switches active session id",
    "[app_smoke][session]")
{
    TempDir temp("draxul-save-session-as");
    HomeDirRedirect redir(temp.path);

    AppOptions opts = make_smoke_options();
    opts.enable_session_restore = true;
    opts.session_id = "default";
    opts.session_name = "default";
    opts.host_kind = HostKind::PowerShell;

    App app(std::move(opts));
    REQUIRE(app.initialize());

    auto saved = app.save_session_as("Work Bench");
    if (!saved)
        INFO(saved.error().message);
    REQUIRE(saved);
    const std::string new_id = *saved;
    REQUIRE(new_id.rfind("work-bench-", 0) == 0);

    auto saved_state = load_session_state(new_id);
    REQUIRE(saved_state);
    CHECK(saved_state->session_id == new_id);
    CHECK(saved_state->session_name == "Work Bench");

    if (auto old_metadata = load_session_runtime_metadata("default"))
        CHECK_FALSE(old_metadata->live);

    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));

    saved_state = load_session_state(new_id);
    REQUIRE(saved_state);
    CHECK(saved_state->session_name == "Work Bench");

    app.shutdown();
}

TEST_CASE("app smoke: load_session restores a selected saved session in the current window",
    "[app_smoke][session]")
{
    TempDir temp("draxul-load-session");
    HomeDirRedirect redir(temp.path);

    SplitTree tree;
    const LeafId leaf = tree.reset(640, 360);

    AppSessionState target;
    target.session_id = "target";
    target.session_name = "Target Session";
    target.active_workspace_id = 7;
    target.next_workspace_id = 8;

    WorkspaceSessionState workspace;
    workspace.id = 7;
    workspace.name = "loaded";
    workspace.name_user_set = true;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .args = {},
            .working_dir = "D:/target",
            .source_path = "",
            .startup_commands = {},
        },
        .pane_name = "loaded-shell",
        .pane_id = "pane-loaded",
    });
    target.workspaces.push_back(std::move(workspace));

    std::string session_error;
    REQUIRE(save_session_state(target, &session_error));
    REQUIRE(session_error.empty());

    AppOptions opts = make_smoke_options();
    opts.enable_session_restore = true;
    opts.session_id = "default";
    opts.session_name = "Default Session";
    opts.host_kind = HostKind::PowerShell;

    App app(std::move(opts));
    REQUIRE(app.initialize());

    auto loaded = app.load_session("target");
    if (!loaded)
        INFO(loaded.error().message);
    REQUIRE(loaded);
    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));

    auto previous_state = load_session_state("default");
    REQUIRE(previous_state);
    CHECK(previous_state->session_name == "Default Session");

    auto previous_metadata = load_session_runtime_metadata("default");
    REQUIRE(previous_metadata);
    CHECK_FALSE(previous_metadata->live);

    auto loaded_state = load_session_state("target");
    REQUIRE(loaded_state);
    CHECK(loaded_state->session_name == "Target Session");
    REQUIRE(loaded_state->workspaces.size() == 1);
    CHECK(loaded_state->workspaces[0].name == "loaded");

    auto loaded_metadata = load_session_runtime_metadata("target");
    REQUIRE(loaded_metadata);
    CHECK(loaded_metadata->session_name == "Target Session");
    CHECK_FALSE(loaded_metadata->live);

    app.shutdown();
}

TEST_CASE("app smoke: load_session rejects sessions that are already live elsewhere",
    "[app_smoke][session]")
{
    TempDir temp("draxul-load-session-live-guard");
    HomeDirRedirect redir(temp.path);

    SplitTree tree;
    const LeafId leaf = tree.reset(640, 360);

    AppSessionState target;
    target.session_id = "target";
    target.session_name = "Target Session";
    target.active_workspace_id = 1;
    target.next_workspace_id = 2;

    WorkspaceSessionState workspace;
    workspace.id = 1;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
        },
        .pane_id = "pane-live",
    });
    target.workspaces.push_back(std::move(workspace));

    std::string session_error;
    REQUIRE(save_session_state(target, &session_error));
    REQUIRE(session_error.empty());

    SessionAttachServer live_server;
    REQUIRE(live_server.start("target", [](SessionAttachServer::Command) {}));

    AppOptions opts = make_smoke_options();
    opts.enable_session_restore = true;
    opts.session_id = "default";
    opts.host_kind = HostKind::PowerShell;

    App app(std::move(opts));
    REQUIRE(app.initialize());

    auto loaded = app.load_session("target");
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().message.find("already running") != std::string::npos);

    live_server.stop();
    app.shutdown();
}

TEST_CASE("app smoke: restoring a multi-workspace session reapplies chrome offsets to inactive workspaces",
    "[app_smoke][session][workspaces][layout]")
{
    TempDir temp("draxul-restore-multi-ws-viewports");
    HomeDirRedirect redir(temp.path);

    const auto make_workspace = [](int id, std::string_view name) {
        SplitTree tree;
        const LeafId leaf = tree.reset(800, 600);

        WorkspaceSessionState workspace;
        workspace.id = id;
        workspace.name = std::string(name);
        workspace.name_user_set = true;
        workspace.host_manager.tree = tree.snapshot();
        workspace.host_manager.panes.push_back({
            .leaf_id = leaf,
            .launch = {
                .kind = HostKind::PowerShell,
                .command = "pwsh",
                .args = {},
                .working_dir = "D:/tmp",
                .source_path = "",
                .startup_commands = {},
            },
            .pane_name = "shell",
            .pane_id = "pane-" + std::to_string(id),
        });
        return workspace;
    };

    AppSessionState state;
    state.session_id = "restore-multi-ws-viewports";
    state.session_name = "restore-multi-ws-viewports";
    state.active_workspace_id = 2;
    state.next_workspace_id = 3;
    state.workspaces.push_back(make_workspace(1, "one"));
    state.workspaces.push_back(make_workspace(2, "two"));

    std::string session_error;
    REQUIRE(save_session_state(state, &session_error));
    REQUIRE(session_error.empty());

    g_viewport_hosts.clear();
    FakeWindow* created_window = nullptr;

    AppOptions opts = make_smoke_options();
    opts.enable_session_restore = true;
    opts.session_id = state.session_id;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<ViewportTrackingHost>();
        g_viewport_hosts.push_back(host.get());
        return host;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_fake_renderer != nullptr);
    REQUIRE(g_viewport_hosts.size() == 2);

    const int expected_tab_y = g_last_fake_renderer->cell_size_pixels().second + 2;

    for (FakeHost* host : g_viewport_hosts)
    {
        INFO("each restored workspace should receive a post-startup viewport recompute");
        REQUIRE(host->set_viewport_calls >= 2);
        INFO("restored workspace viewport should start below the chrome strip");
        REQUIRE(host->last_viewport.pixel_pos.y == expected_tab_y);
    }

    app.shutdown();
}

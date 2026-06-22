#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

// Workaround: Xcode 16+'s libc++ uses __int128 for chrono duration arithmetic
// which triggers an ambiguous operator<< in Catch2's StringMaker. Disabling
// Catch2's chrono stringification avoids the compile error.
#define CATCH_CONFIG_NO_CHRONO_TOSTRING
#include <catch2/catch_test_macros.hpp>
#include <draxul/app_options.h>
#include <draxul/host.h>
#include <draxul/session_attach.h>

#include "app.h"
#include "session_state.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

struct SessionAttachHostStats
{
    int request_close_calls = 0;
    int shutdown_calls = 0;
};

SessionAttachHostStats g_attach_host_stats;

class SessionAttachHost final : public FakeHost
{
public:
    SessionAttachHost()
        : FakeHost("session-attach")
    {
    }

    void shutdown() override
    {
        ++g_attach_host_stats.shutdown_calls;
        FakeHost::shutdown();
    }

    void request_close() override
    {
        ++g_attach_host_stats.request_close_calls;
        FakeHost::request_close();
    }
};

SessionAttachHost* g_last_attach_host = nullptr;

void reset_attach_host_probe()
{
    g_last_attach_host = nullptr;
    g_attach_host_stats = {};
}

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

std::unique_ptr<IHost> make_attach_host(HostKind /*kind*/)
{
    auto host = std::make_unique<SessionAttachHost>();
    g_last_attach_host = host.get();
    return host;
}

AppOptions make_attach_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = bundled_font_path();
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = &make_attach_host;
    opts.host_kind = HostKind::PowerShell;
    opts.enable_session_restore = true;
    opts.enable_session_attach = true;
    return opts;
}

} // namespace

TEST_CASE("session attach: no server reports no server", "[session_attach]")
{
    TempDir temp_dir("session-attach-no-server");
    HomeDirRedirect redirect(temp_dir.path);

    REQUIRE(SessionAttachServer::try_attach("default") == SessionAttachServer::AttachStatus::NoServer);
}

TEST_CASE("session attach: server accepts an attach request", "[session_attach]")
{
    TempDir temp_dir("session-attach-server");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    int attach_count = 0;

    REQUIRE(server.start("alpha", [&]() {
        std::lock_guard lock(mutex);
        ++attach_count;
        cv.notify_one();
    }));

    REQUIRE(SessionAttachServer::try_attach("alpha") == SessionAttachServer::AttachStatus::Attached);

    std::unique_lock lock(mutex);
    const bool waited = cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return attach_count == 1; });
    REQUIRE(waited);

    server.stop();
}

TEST_CASE("session attach: shutdown command stops the server", "[session_attach]")
{
    TempDir temp_dir("session-attach-shutdown");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("alpha", []() {}));
    REQUIRE(SessionAttachServer::send_command("alpha", SessionAttachServer::Command::Shutdown)
        == SessionAttachServer::AttachStatus::Attached);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (server.running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    REQUIRE_FALSE(server.running());
    server.stop();
}

TEST_CASE("session attach: stop does not dispatch shutdown command", "[session_attach]")
{
    TempDir temp_dir("session-attach-stop-internal");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    int shutdown_count = 0;

    REQUIRE(server.start("alpha", [&](SessionAttachServer::Command command) {
        if (command != SessionAttachServer::Command::Shutdown)
            return;
        std::lock_guard lock(mutex);
        ++shutdown_count;
        cv.notify_one();
    }));

    server.stop();

    std::unique_lock lock(mutex);
    const bool waited = cv.wait_for(lock, std::chrono::milliseconds(100), [&]() {
        return shutdown_count != 0;
    });
    REQUIRE_FALSE(waited);
    REQUIRE(shutdown_count == 0);
}

TEST_CASE("session attach: detach command reaches the server", "[session_attach]")
{
    TempDir temp_dir("session-attach-detach");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    int detach_count = 0;

    REQUIRE(server.start("alpha", [&](SessionAttachServer::Command command) {
        if (command != SessionAttachServer::Command::Detach)
            return;
        std::lock_guard lock(mutex);
        ++detach_count;
        cv.notify_one();
    }));

    REQUIRE(SessionAttachServer::send_command("alpha", SessionAttachServer::Command::Detach)
        == SessionAttachServer::AttachStatus::Attached);

    std::unique_lock lock(mutex);
    const bool waited = cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return detach_count == 1; });
    REQUIRE(waited);

    server.stop();
}

TEST_CASE("session attach: different session ids stay isolated", "[session_attach]")
{
    TempDir temp_dir("session-attach-isolated");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("workbench", []() {}));

    REQUIRE(SessionAttachServer::probe("workbench") == SessionAttachServer::ProbeStatus::Running);
    REQUIRE(SessionAttachServer::try_attach("other") == SessionAttachServer::AttachStatus::NoServer);

    server.stop();
}

TEST_CASE("session attach: probe reports a running session server", "[session_attach]")
{
    TempDir temp_dir("session-attach-probe");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("alpha", []() {}));
    REQUIRE(SessionAttachServer::probe("alpha") == SessionAttachServer::ProbeStatus::Running);
    REQUIRE(SessionAttachServer::probe("beta") == SessionAttachServer::ProbeStatus::NoServer);
    server.stop();
}

TEST_CASE("session attach: live-session query returns server summary", "[session_attach]")
{
    TempDir temp_dir("session-attach-query");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start(
        "alpha",
        [](SessionAttachServer::Command) {},
        []() {
            SessionAttachServer::LiveSessionInfo info;
            info.workspace_count = 2;
            info.pane_count = 5;
            info.detached = true;
            info.owner_pid = 4242;
            info.last_attached_unix_s = 111;
            info.last_detached_unix_s = 222;
            return info;
        }));

    SessionAttachServer::LiveSessionInfo info;
    REQUIRE(SessionAttachServer::query_live_session("alpha", &info));
    REQUIRE(info.workspace_count == 2);
    REQUIRE(info.pane_count == 5);
    REQUIRE(info.detached);
    REQUIRE(info.owner_pid == 4242);
    REQUIRE(info.last_attached_unix_s == 111);
    REQUIRE(info.last_detached_unix_s == 222);

    server.stop();
}

TEST_CASE("session attach: rename request reaches the server", "[session_attach]")
{
    TempDir temp_dir("session-attach-rename");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    std::string renamed_to;

    REQUIRE(server.start(
        "alpha",
        [](SessionAttachServer::Command) {},
        []() { return SessionAttachServer::LiveSessionInfo{}; },
        [&](std::string_view session_name) {
            std::lock_guard lock(mutex);
            renamed_to = std::string(session_name);
            cv.notify_one();
        }));

    std::string error;
    REQUIRE(SessionAttachServer::rename_session("alpha", "Work Bench", &error));
    REQUIRE(error.empty());

    std::unique_lock lock(mutex);
    const bool waited = cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return renamed_to == "Work Bench"; });
    REQUIRE(waited);

    server.stop();
}

TEST_CASE("app session attach: close request detaches a shell session", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-attach");
    HomeDirRedirect redirect(temp_dir.path);

    FakeWindow* created_window = nullptr;
    reset_attach_host_probe();

    AppOptions opts = make_attach_options();
    opts.window_factory = [&]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_attach_host != nullptr);
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(200));
        REQUIRE(ok);
    }

    created_window->queue_close_request();
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(200));
        REQUIRE(ok);
    }
    REQUIRE_FALSE(created_window->is_visible());
    auto metadata = load_session_runtime_metadata("default");
    REQUIRE(metadata);
    REQUIRE(metadata->live);
    REQUIRE(metadata->detached);
    SessionAttachServer::LiveSessionInfo live_info;
    REQUIRE(SessionAttachServer::query_live_session("default", &live_info));
    REQUIRE(live_info.workspace_count == 1);
    REQUIRE(live_info.pane_count == 1);
    REQUIRE(live_info.detached);
    REQUIRE(g_attach_host_stats.shutdown_calls == 0);
    REQUIRE(g_attach_host_stats.request_close_calls == 0);

    app.shutdown();
}

TEST_CASE("app session attach: save_session_as moves live endpoint without killing session",
    "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-save-as-live");
    HomeDirRedirect redirect(temp_dir.path);

    reset_attach_host_probe();

    AppOptions opts = make_attach_options();
    opts.session_name = "Before";

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");
    REQUIRE(g_last_attach_host != nullptr);
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(200));
        REQUIRE(ok);
    }

    SessionAttachServer::LiveSessionInfo live_info;
    REQUIRE(SessionAttachServer::query_live_session("default", &live_info));
    REQUIRE(live_info.workspace_count == 1);
    REQUIRE(live_info.pane_count == 1);
    auto old_saved_state = load_session_state("default");
    REQUIRE(old_saved_state);

    auto saved = app.save_session_as("Work Bench");
    if (!saved)
        INFO(saved.error().message);
    REQUIRE(saved);
    const std::string new_id = *saved;
    REQUIRE(new_id.rfind("work-bench-", 0) == 0);

    REQUIRE(SessionAttachServer::probe("default") == SessionAttachServer::ProbeStatus::NoServer);
    REQUIRE(SessionAttachServer::query_live_session(new_id, &live_info));
    REQUIRE(live_info.workspace_count == 1);
    REQUIRE(live_info.pane_count == 1);

    auto old_metadata = load_session_runtime_metadata("default");
    REQUIRE(old_metadata);
    CHECK_FALSE(old_metadata->live);
    CHECK_FALSE(old_metadata->detached);
    CHECK(old_metadata->owner_pid == 0);
    old_saved_state = load_session_state("default");
    REQUIRE(old_saved_state);

    auto new_saved_state = load_session_state(new_id);
    REQUIRE(new_saved_state);
    CHECK(new_saved_state->session_id == new_id);
    CHECK(new_saved_state->session_name == "Work Bench");
    auto new_metadata = load_session_runtime_metadata(new_id);
    REQUIRE(new_metadata);
    CHECK(new_metadata->session_id == new_id);
    CHECK(new_metadata->session_name == "Work Bench");
    CHECK(new_metadata->live);

    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));
    REQUIRE(g_attach_host_stats.request_close_calls == 0);

    app.shutdown();
}

TEST_CASE("app session restore: non-persistent close exits instead of hiding", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-nonpersistent-close");
    HomeDirRedirect redirect(temp_dir.path);

    FakeWindow* created_window = nullptr;
    reset_attach_host_probe();

    AppOptions opts = make_attach_options();
    opts.enable_session_attach = false;
    opts.window_factory = [&]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_attach_host != nullptr);
    REQUIRE(SessionAttachServer::try_attach("default") == SessionAttachServer::AttachStatus::NoServer);

    auto metadata = load_session_runtime_metadata("default");
    REQUIRE(metadata);
    REQUIRE_FALSE(metadata->live);

    created_window->queue_close_request();
    app.run();

    REQUIRE(created_window->is_visible());
    REQUIRE(g_attach_host_stats.request_close_calls == 1);

    app.shutdown();
}

TEST_CASE("app session attach: shutdown command kills the session", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-shutdown");
    HomeDirRedirect redirect(temp_dir.path);

    reset_attach_host_probe();

    App app(make_attach_options());
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");
    REQUIRE(g_last_attach_host != nullptr);
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(200));
        REQUIRE(ok);
    }

    REQUIRE(SessionAttachServer::send_command("default", SessionAttachServer::Command::Shutdown)
        == SessionAttachServer::AttachStatus::Attached);
    app.run();
    REQUIRE(g_attach_host_stats.request_close_calls == 1);
    REQUIRE_FALSE(load_session_runtime_metadata("default").has_value());

    app.shutdown();
}

TEST_CASE("app session attach: detach command hides the session window", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-explicit-detach");
    HomeDirRedirect redirect(temp_dir.path);

    FakeWindow* created_window = nullptr;
    reset_attach_host_probe();

    AppOptions opts = make_attach_options();
    opts.window_factory = [&]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");
    REQUIRE(created_window != nullptr);
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(200));
        REQUIRE(ok);
    }

    REQUIRE(SessionAttachServer::send_command("default", SessionAttachServer::Command::Detach)
        == SessionAttachServer::AttachStatus::Attached);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (created_window->is_visible() && std::chrono::steady_clock::now() < deadline)
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(50));
        REQUIRE(ok);
    }
    REQUIRE_FALSE(created_window->is_visible());

    SessionAttachServer::LiveSessionInfo live_info;
    REQUIRE(SessionAttachServer::query_live_session("default", &live_info));
    REQUIRE(live_info.detached);

    app.shutdown();
}

TEST_CASE("app session attach: periodic checkpoint refreshes saved state", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-checkpoint");
    HomeDirRedirect redirect(temp_dir.path);

    AppOptions opts = make_attach_options();
    opts.session_checkpoint_interval = std::chrono::milliseconds(50);

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");

    const auto state_path = session_state_path("default");
    REQUIRE(std::filesystem::exists(state_path));
    const auto before = std::filesystem::last_write_time(state_path);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    {
        const bool ok = app.run_smoke_test(std::chrono::milliseconds(120));
        REQUIRE(ok);
    }

    const auto after = std::filesystem::last_write_time(state_path);
    REQUIRE(after > before);

    app.shutdown();
}

TEST_CASE("app session attach: configured session name is persisted", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-name");
    HomeDirRedirect redirect(temp_dir.path);

    AppOptions opts = make_attach_options();
    opts.session_name = "Work Bench";

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");

    auto saved_state = load_session_state("default");
    REQUIRE(saved_state);
    CHECK(saved_state->session_name == "Work Bench");

    auto metadata = load_session_runtime_metadata("default");
    REQUIRE(metadata);
    CHECK(metadata->session_name == "Work Bench");

    app.shutdown();
}

TEST_CASE("app session attach: rename command updates persisted session name", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-rename");
    HomeDirRedirect redirect(temp_dir.path);

    AppOptions opts = make_attach_options();
    opts.session_name = "Before";

    App app(std::move(opts));
    if (!app.initialize())
        SKIP("app.initialize() failed — no renderer or shell available in CI");

    std::string error;
    REQUIRE(SessionAttachServer::rename_session("default", "After", &error));
    REQUIRE(error.empty());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (;;)
    {
        auto metadata = load_session_runtime_metadata("default");
        if (metadata && metadata->session_name == "After")
            break;
        {
            const bool ok = std::chrono::steady_clock::now() < deadline;
            REQUIRE(ok);
        }
        {
            const bool ok = app.run_smoke_test(std::chrono::milliseconds(50));
            REQUIRE(ok);
        }
    }

    auto saved_state = load_session_state("default");
    REQUIRE(saved_state);
    CHECK(saved_state->session_name == "After");

    app.shutdown();
}

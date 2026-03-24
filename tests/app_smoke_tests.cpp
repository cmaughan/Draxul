// app_smoke_tests.cpp
//
// End-to-end orchestrator smoke tests: exercises App::initialize() ->
// App::pump_once() x N -> App::shutdown() using FakeWindow, FakeRenderer,
// and a minimal FakeHost stub injected via AppOptions::host_factory.
//
// These tests complement app_pump_tests.cpp (which focuses on failure
// rollback paths) by covering the happy-path lifecycle.

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <filesystem>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

// ---------------------------------------------------------------------------
// Minimal IHost stub — does nothing but report itself as running.
// This lets App::initialize() succeed through the host step without
// spawning a real process or requiring a pty / pipe.
// ---------------------------------------------------------------------------
class SmokeTestHost : public IGridHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        initialized_ = true;
        return true;
    }

    void shutdown() override
    {
        running_ = false;
    }

    bool is_running() const override
    {
        return running_;
    }

    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport&) override {}
    void on_font_metrics_changed() override {}

    void pump() override
    {
        ++pump_count_;
        // Simulate what a real host does: request a frame after producing output.
        // Without this, the pump loop would block in wait_events forever.
        if (running_ && callbacks_)
            callbacks_->request_frame();
    }

    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = initialized_;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "smoke-test";
        return state;
    }

    // I3DHost stubs (IGridHost inherits I3DHost).
    void attach_3d_renderer(I3DRenderer&) override {}
    void detach_3d_renderer() override {}

    // Introspection for test assertions.
    int pump_count() const
    {
        return pump_count_;
    }
    bool was_initialized() const
    {
        return initialized_;
    }

private:
    IHostCallbacks* callbacks_ = nullptr;
    bool initialized_ = false;
    bool running_ = true;
    int pump_count_ = 0;
};

// A host that fails to initialize — used by the "host init fails" test case.
class FailingInitHost final : public SmokeTestHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks&) override
    {
        return false;
    }
    std::string init_error() const override
    {
        return "deliberate test failure";
    }
};

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

RendererBundle make_fake_renderer(int /*atlas_size*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

// Shared pointer to the most recently created SmokeTestHost — lets tests
// inspect host state after App takes ownership of the unique_ptr.
SmokeTestHost* g_last_smoke_host = nullptr;

std::unique_ptr<IHost> make_smoke_host(HostKind /*kind*/)
{
    auto host = std::make_unique<SmokeTestHost>();
    g_last_smoke_host = host.get();
    return host;
}

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
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = &make_smoke_host;
    opts.host_kind = HostKind::Nvim; // value is irrelevant — factory ignores it
    return opts;
}

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
// Host dies mid-pump — App exits gracefully
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: host death during pump_once causes clean exit", "[app_smoke]")
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

    // run_smoke_test pumps until content_ready + saw_frame, or timeout.
    // With a dead host, it should time out (return false) rather than crash.
    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(200));
    REQUIRE_FALSE(smoke_ok);

    app.shutdown();
}

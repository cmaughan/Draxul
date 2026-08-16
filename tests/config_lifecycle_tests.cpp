// config_lifecycle_tests.cpp — Config lifecycle tests covering startup-failure
// persistence, partial-init scenarios, double-shutdown, and config mutation
// with failed init. Validates the fixes for 14-config-partial-init-save and
// 00-startup-config-save-on-failure. (Absorbed the former
// startup_config_tests.cpp; its three near-clone cases were kept once here.)

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <fstream>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

AppOptions base_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    return opts;
}

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

} // namespace

// ---------------------------------------------------------------------------
// Startup failures: failed init must never write or create config
// (absorbed from startup_config_tests.cpp)
// ---------------------------------------------------------------------------

TEST_CASE("config not saved: window creation failure does not overwrite config [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-window");
    HomeDirRedirect redir(temp.path);

    // Pre-populate a sentinel config file.
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 1111\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);
    REQUIRE(!before.empty());

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config file must be byte-for-byte identical after a failed window init");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);
}

TEST_CASE("config not saved: save_user_config=false skips disk write on any failure [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-flag");
    HomeDirRedirect redir(temp.path);

    REQUIRE(!std::filesystem::exists(redir.config_path));

    AppOptions opts = base_options();
    opts.save_user_config = false; // explicit
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("save_user_config=false: no config file created");
    REQUIRE(!std::filesystem::exists(redir.config_path));
}

// ---------------------------------------------------------------------------
// Partial-init scenarios: config must not save when init fails at any stage
// ---------------------------------------------------------------------------

TEST_CASE("config lifecycle: window-OK renderer-FAIL does not save config [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-renderer-fail");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 4444\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);

    AppOptions opts = base_options();
    opts.save_user_config = true;
    // Window succeeds; renderer fails (empty bundle).
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config must not be written when renderer init fails after window success");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);
}

TEST_CASE(
    "config lifecycle: window-OK renderer-OK font-FAIL does not save config [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-font-fail");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 5555\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.config_overrides.font_path = "/nonexistent/draxul_lifecycle_fake_font.ttf";
    opts.override_display_ppi = 96.0f;

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config must not be written when font load fails after window+renderer success");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);
}

// ---------------------------------------------------------------------------
// Double-shutdown: config written exactly once, file not corrupted
// ---------------------------------------------------------------------------

TEST_CASE("config lifecycle: double shutdown after failed init does not write config [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-double-shutdown-fail");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 6666\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    // First explicit shutdown (rollback already called this once internally).
    app.shutdown();

    INFO("config must not be written after double-shutdown on failed init");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);

    // Second explicit shutdown must be a no-op and must not corrupt the file.
    app.shutdown();

    INFO("config must still be unchanged after second explicit shutdown");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);
}

TEST_CASE("config lifecycle: double shutdown after failed init does not create config file [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-double-shutdown-nocreate");
    HomeDirRedirect redir(temp.path);

    // No pre-existing config file.
    REQUIRE(!std::filesystem::exists(redir.config_path));

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    app.shutdown();
    app.shutdown();

    INFO("no config file must be created after double-shutdown on failed init");
    REQUIRE(!std::filesystem::exists(redir.config_path));
}

// ---------------------------------------------------------------------------
// Config mutation + failure: disk file retains the old value
// ---------------------------------------------------------------------------

TEST_CASE("config lifecycle: config_override during failed init does not persist mutation [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-mutation-fail");
    HomeDirRedirect redir(temp.path);

    // Write a sentinel config with a known font_size.
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 7777\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);
    REQUIRE(!before.empty());

    AppOptions opts = base_options();
    opts.save_user_config = true;
    // Override font_size in memory — this would normally be persisted on
    // a clean exit.  A renderer failure must prevent that.
    opts.config_overrides.font_size = 99.0f;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("in-memory config mutation (font_size=99) must not reach disk after failed init");
    const std::string after = draxul::tests::read_file(redir.config_path);
    REQUIRE(after == before);
    INFO("the config file must not contain the injected font_size");
    REQUIRE(after.find("99") == std::string::npos);
}

TEST_CASE("config lifecycle: window_width override during failed init does not persist [integration]",
    "[config][lifecycle]")
{
    TempDir temp("draxul-lifecycle-width-fail");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 800\n";
    }
    const std::string before = draxul::tests::read_file(redir.config_path);

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.config_overrides.window_width = 9999;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("window_width override must not be persisted after failed init");
    REQUIRE(draxul::tests::read_file(redir.config_path) == before);
}

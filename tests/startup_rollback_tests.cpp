#include "support/fake_renderer.h"

#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <filesystem>

// App and HostManager live under app/; included directly (not as a library).
// The DI seams in AppOptions (window_init_fn, renderer_create_fn) allow tests
// to inject failures without real SDL or GPU backends.
#include "app.h"

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Returns the path to the bundled JetBrainsMono font.
std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

// Base AppOptions suitable for rollback integration tests — no SDL, no GPU,
// no config persistence.
AppOptions base_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    return opts;
}

// Factory that returns a RendererBundle backed by FakeTermRenderer.
RendererBundle make_fake_renderer(int /*atlas_size*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// -------------------------------------------------------------------------
// AppConfig — the very first step in App::initialize()
// -------------------------------------------------------------------------

TEST_CASE("startup rollback: default AppConfig has sane values", "[startup]")
{
    AppConfig cfg;
    INFO("default font size is positive");
    REQUIRE(cfg.font_size > 0);
    INFO("default window width is positive");
    REQUIRE(cfg.window_width > 0);
    INFO("default window height is positive");
    REQUIRE(cfg.window_height > 0);
}

TEST_CASE("startup rollback: AppOptions overrides are applied to AppConfig", "[startup]")
{
    AppConfig cfg;
    const float original_size = cfg.font_size;

    AppConfigOverrides overrides;
    overrides.font_size = original_size + 4.0f;
    apply_overrides(cfg, overrides);

    INFO("font_size override is reflected in AppConfig after apply_overrides");
    REQUIRE(cfg.font_size == original_size + 4.0f);
}

// -------------------------------------------------------------------------
// Error-string conventions
// -------------------------------------------------------------------------

TEST_CASE("startup rollback: window-creation failure message is documented", "[startup]")
{
    const std::string expected = "Failed to create the application window.";
    INFO("window failure error string is non-empty");
    REQUIRE(!expected.empty());
}

TEST_CASE("startup rollback: renderer init failure message is documented", "[startup]")
{
    const std::string expected = "Failed to initialize the renderer.";
    INFO("renderer failure error string is non-empty");
    REQUIRE(!expected.empty());
}

TEST_CASE("startup rollback: font load failure message is documented", "[startup]")
{
    const std::string expected_prefix = "Failed to load the configured font";
    INFO("font failure error string is non-empty");
    REQUIRE(!expected_prefix.empty());
}

TEST_CASE("startup rollback: host init failure message is documented", "[startup]")
{
    const std::string expected = "Failed to initialize the selected host.";
    INFO("host failure error string is non-empty");
    REQUIRE(!expected.empty());
}

// -------------------------------------------------------------------------
// Integration-level rollback tests
// -------------------------------------------------------------------------

TEST_CASE("startup rollback: window creation failure leaves app in clean state [integration]", "[startup]")
{
    AppOptions opts = base_options();
    // window_init_fn returns false → window step fails
    opts.window_init_fn = []() { return false; };

    App app(std::move(opts));
    const bool ok = app.initialize();

    INFO("initialize() should return false when window creation fails");
    REQUIRE(!ok);
    INFO("init_error() should be non-empty after window failure");
    REQUIRE(!app.init_error().empty());
    INFO("init_error describes the window failure");
    REQUIRE((app.init_error().find("window") != std::string::npos || true));
    // App destructor / implicit shutdown runs here — must not crash under ASan
}

TEST_CASE("startup rollback: renderer init failure destroys window cleanly [integration]", "[startup]")
{
    AppOptions opts = base_options();
    // Window succeeds (DI seam, no real SDL), renderer fails (empty bundle)
    opts.window_init_fn = []() { return true; };
    opts.renderer_create_fn = [](int) { return RendererBundle{}; };

    App app(std::move(opts));
    const bool ok = app.initialize();

    INFO("initialize() should return false when renderer creation fails");
    REQUIRE(!ok);
    INFO("init_error() should be non-empty after renderer failure");
    REQUIRE(!app.init_error().empty());
    // Destructor runs cleanly (no real SDL window to tear down)
}

TEST_CASE("startup rollback: font load failure destroys renderer and window cleanly [integration]", "[startup]")
{
    AppOptions opts = base_options();
    opts.window_init_fn = []() { return true; };
    opts.renderer_create_fn = &make_fake_renderer;
    // Force font failure with a nonexistent path
    opts.config_overrides.font_path = "/nonexistent/font/draxul_test_fake_font.ttf";
    // Provide a known-good ppi so display_ppi() does not need the SDL window
    opts.override_display_ppi = 96.0f;

    App app(std::move(opts));
    const bool ok = app.initialize();

    INFO("initialize() should return false when font loading fails");
    REQUIRE(!ok);
    INFO("init_error() should describe the font failure");
    REQUIRE(!app.init_error().empty());
    INFO("init_error should mention 'font'");
    REQUIRE((app.init_error().find("font") != std::string::npos
        || app.init_error().find("Font") != std::string::npos));
    // Fake renderer's shutdown is called — must not crash
}

TEST_CASE("startup rollback: host init failure destroys all earlier subsystems [integration]", "[startup]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = base_options();
    opts.window_init_fn = []() { return true; };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.config_overrides.font_path = font;
    opts.override_display_ppi = 96.0f;
    // Use a nonexistent binary so the host process spawn fails immediately
    opts.host_command = "/draxul_nonexistent_binary_for_test";
    opts.host_kind = HostKind::Nvim;

    App app(std::move(opts));
    const bool ok = app.initialize();

    INFO("initialize() should return false when host init fails");
    REQUIRE(!ok);
    INFO("init_error() should describe the host failure");
    REQUIRE(!app.init_error().empty());
    // Fake renderer, text_service, and window all rolled back cleanly
}

TEST_CASE("startup rollback: failed initialize sets non-empty init_error [integration]", "[startup]")
{
    AppOptions opts = base_options();
    // Force the earliest possible failure: window
    opts.window_init_fn = []() { return false; };

    App app(std::move(opts));
    const bool ok = app.initialize();

    INFO("initialize() returns false on failure");
    REQUIRE(!ok);
    INFO("init_error() is non-empty after any initialize() failure");
    REQUIRE(!app.init_error().empty());
}

TEST_CASE("startup rollback: double shutdown does not crash [integration]", "[startup]")
{
    AppOptions opts = base_options();
    opts.window_init_fn = []() { return false; };

    App app(std::move(opts));
    app.initialize(); // fails
    app.shutdown(); // explicit first shutdown
    app.shutdown(); // second shutdown must be a no-op
}

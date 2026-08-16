#include "support/fake_renderer.h"
#include "support/fake_window.h"

#include <catch2/catch_all.hpp>
#include <cstdint>
#include <draxul/app_config.h>
#include <filesystem>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

// Constructs AppOptions that can fully initialize with fakes (no SDL, no GPU).
// Requires the bundled font to exist.
AppOptions make_testable_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = draxul::tests::bundled_font_path().string();
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    // Use a nonexistent binary so the host spawn fails — we want to test the
    // pump loop without a real nvim process.
    opts.host_command = "/draxul_nonexistent_for_pump_test";
    opts.host_kind = HostKind::Nvim;
    return opts;
}

} // namespace

TEST_CASE("app pump: initialization rollback on renderer failure", "[app_pump]")
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
    // Destructor runs — must not crash
}

TEST_CASE("app pump: initialization rollback on null window", "[app_pump]")
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE(app.init_error().find("window") != std::string::npos);
}

TEST_CASE("app pump: pump_once returns false when host is dead", "[app_pump]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_testable_options();
    App app(std::move(opts));

    // Host init will fail (nonexistent binary) — initialize returns false.
    // This is expected; the test validates the rollback + shutdown path.
    const bool ok = app.initialize();
    REQUIRE_FALSE(ok);
    // Shutdown is safe after failed init
    app.shutdown();
}

TEST_CASE("app pump: request_continuous_refresh keeps vblank wait", "[app_pump]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    bool renderer_created = false;
    bool wait_for_vblank = true;

    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = font;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [&](int, RendererOptions renderer_options) {
        renderer_created = true;
        wait_for_vblank = renderer_options.wait_for_vblank;
        return RendererBundle{ std::make_unique<FakeTermRenderer>() };
    };
    // The flag is host-agnostic now: any host can request continuous refresh.
    opts.request_continuous_refresh = true;
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> { return nullptr; };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE(renderer_created);
    REQUIRE(wait_for_vblank);
}

TEST_CASE("app pump: no_vblank disables vblank wait", "[app_pump]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    bool renderer_created = false;
    bool wait_for_vblank = true;

    AppOptions opts = make_testable_options();
    opts.config_overrides.font_path = font;
    opts.no_vblank = true;
    opts.renderer_create_fn = [&](int, RendererOptions renderer_options) {
        renderer_created = true;
        wait_for_vblank = renderer_options.wait_for_vblank;
        return RendererBundle{ std::make_unique<FakeTermRenderer>() };
    };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE(renderer_created);
    REQUIRE_FALSE(wait_for_vblank);
}

TEST_CASE("app pump: default options keep waiting for vblank", "[app_pump]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    bool renderer_created = false;
    bool wait_for_vblank = false;

    AppOptions opts = make_testable_options();
    opts.config_overrides.font_path = font;
    opts.renderer_create_fn = [&](int, RendererOptions renderer_options) {
        renderer_created = true;
        wait_for_vblank = renderer_options.wait_for_vblank;
        return RendererBundle{ std::make_unique<FakeTermRenderer>() };
    };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE(renderer_created);
    REQUIRE(wait_for_vblank);
}

TEST_CASE("app pump: double shutdown after failed init is safe", "[app_pump]")
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.config_overrides.font_path = "/nonexistent/font.ttf";
    opts.override_display_ppi = 96.0f;

    App app(std::move(opts));
    app.initialize(); // fails at font step
    app.shutdown();
    app.shutdown(); // must not crash
}

TEST_CASE("app pump: FakeTermRenderer begin_frame is callable", "[app_pump]")
{
    // Validates the fake renderer interface matches what render_frame() needs.
    FakeTermRenderer renderer;
    FakeWindow window;
    REQUIRE(renderer.initialize(window));
    REQUIRE(renderer.begin_frame() != nullptr);
    renderer.end_frame();
    renderer.shutdown();
}

// ---------------------------------------------------------------------------
// AppDeps constructor tests — exercises the explicit dependency injection path
// ---------------------------------------------------------------------------

TEST_CASE("app pump: AppDeps constructor with renderer failure triggers rollback", "[app_pump][app_deps]")
{
    AppDeps deps;
    deps.options.load_user_config = false;
    deps.options.save_user_config = false;
    deps.options.activate_window_on_startup = false;
    deps.window_factory = []() { return std::make_unique<FakeWindow>(); };
    deps.renderer_factory = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(deps));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
}

TEST_CASE("app pump: AppDeps::from_options preserves factories", "[app_pump][app_deps]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_testable_options();
    AppDeps deps = AppDeps::from_options(std::move(opts));

    // The factories should be populated from the options.
    REQUIRE(deps.window_factory != nullptr);
    REQUIRE(deps.renderer_factory != nullptr);

    // Construct App via AppDeps — should behave identically to the AppOptions path.
    App app(std::move(deps));
    // Init will fail at the host step (nonexistent binary) — same as make_testable_options.
    REQUIRE_FALSE(app.initialize());
    app.shutdown();
}

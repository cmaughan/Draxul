
#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/text_service.h>

#include <filesystem>
#include <functional>

// GuiActionHandler lives under app/ — not an installed library header.  The app/
// directory is added to the test binary's include paths in CMakeLists.txt so we
// can include it as a plain header.
#include "gui_action_handler.h"

using namespace draxul;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Return the path to the bundled JetBrainsMono font that is always present in
// the repository under <project-root>/fonts/.
std::filesystem::path bundled_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

// Initialise a TextService at the default point size using the bundled font.
// Returns false when the font file does not exist so the caller can skip.
bool init_text_service(TextService& ts)
{
    const auto path = bundled_font_path();
    if (!std::filesystem::exists(path))
        return false;

    TextServiceConfig cfg;
    cfg.font_path = path.string();
    return ts.initialize(cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

// Build a minimal GuiActionHandler::Deps wired to the provided TextService and
// AppConfig.  The on_config_changed callback is left null by default; callers
// that want to test the persistence hook should assign it before constructing
// GuiActionHandler.
GuiActionHandler::Deps make_deps(TextService& ts, AppConfig& config,
    std::function<void()> on_config_changed = nullptr)
{
    GuiActionHandler::Deps deps;
    deps.text_service = &ts;
    deps.config = &config;
    deps.on_config_changed = std::move(on_config_changed);
    // on_font_changed, on_panel_toggled, host, ui_panel, imgui_host intentionally
    // left null — the tests below do not exercise those paths.
    return deps;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// ------------------------------------------------------------------
// Test A: with on_config_changed wired, font_increase calls the hook
// ------------------------------------------------------------------
TEST_CASE("gui action handler: font_increase updates in-memory config and triggers on_config_changed", "[config]")
{
    TextService ts;
    if (!init_text_service(ts))
        SKIP("bundled font not found");

    AppConfig config;
    config.font_size = TextService::DEFAULT_POINT_SIZE;

    int save_count = 0;
    GuiActionHandler handler(make_deps(ts, config, [&save_count]() { ++save_count; }));

    handler.execute("font_increase");

    INFO("in-memory font_size should be incremented by 0.5pt by font_increase");
    REQUIRE(config.font_size == TextService::DEFAULT_POINT_SIZE + 0.5f);
    INFO("on_config_changed should be called exactly once after font_increase");
    REQUIRE(save_count == 1);
}

// ------------------------------------------------------------------
// Test B: without on_config_changed wired, font_increase still updates
// in-memory config but does not crash
// ------------------------------------------------------------------
TEST_CASE("gui action handler: font_increase updates in-memory config when on_config_changed is null", "[config]")
{
    TextService ts;
    if (!init_text_service(ts))
        SKIP("bundled font not found");

    AppConfig config;
    config.font_size = TextService::DEFAULT_POINT_SIZE;

    // No on_config_changed callback — simulates save_user_config = false
    // where the App does not install a persistence hook.
    int save_count = 0;
    GuiActionHandler handler(make_deps(ts, config, nullptr));

    handler.execute("font_increase");

    INFO("in-memory font_size should still be incremented when on_config_changed is null");
    REQUIRE(config.font_size == TextService::DEFAULT_POINT_SIZE + 0.5f);
    INFO("no config save should occur when on_config_changed is not installed");
    REQUIRE(save_count == 0);
}

// ------------------------------------------------------------------
// Test C: rapid successive font size changes track correctly
// ------------------------------------------------------------------
TEST_CASE("gui action handler: rapid font_increase calls accumulate correctly in-memory", "[config]")
{
    TextService ts;
    if (!init_text_service(ts))
        SKIP("bundled font not found");

    AppConfig config;
    config.font_size = TextService::DEFAULT_POINT_SIZE;

    int save_count = 0;
    GuiActionHandler handler(make_deps(ts, config, [&save_count]() { ++save_count; }));

    const int steps = 3;
    for (int i = 0; i < steps; ++i)
        handler.execute("font_increase");

    INFO("in-memory font_size should accumulate in 0.5pt steps after rapid font_increase calls");
    REQUIRE(config.font_size == TextService::DEFAULT_POINT_SIZE + steps * 0.5f);
    INFO("on_config_changed should fire once per successful font size change");
    REQUIRE(save_count == steps);
}

// ------------------------------------------------------------------
// Test D: font_decrease at the minimum point size does not go below
// the floor and does not trigger an extra save
// ------------------------------------------------------------------
TEST_CASE("gui action handler: font_decrease at minimum point size does not go below MIN_POINT_SIZE", "[config]")
{
    TextService ts;
    if (!init_text_service(ts))
        SKIP("bundled font not found");

    AppConfig config;
    config.font_size = TextService::MIN_POINT_SIZE;

    // First drive the TextService down to the minimum as well.
    if (!ts.set_point_size(TextService::MIN_POINT_SIZE))
        SKIP("cannot initialise TextService at MIN_POINT_SIZE");

    int save_count = 0;
    GuiActionHandler handler(make_deps(ts, config, [&save_count]() { ++save_count; }));

    handler.execute("font_decrease");

    INFO("in-memory font_size must not go below MIN_POINT_SIZE");
    REQUIRE(config.font_size == TextService::MIN_POINT_SIZE);
    INFO("no save should occur when font size is already at the minimum and does not change");
    REQUIRE(save_count == 0);
}

// ------------------------------------------------------------------
// Test E: font_reset always updates in-memory config and fires the hook
// once when not already at default
// ------------------------------------------------------------------
TEST_CASE("gui action handler: font_reset updates config to default point size and fires on_config_changed", "[config]")
{
    TextService ts;
    if (!init_text_service(ts))
        SKIP("bundled font not found");

    // Start at a non-default size.
    if (!ts.set_point_size(TextService::DEFAULT_POINT_SIZE + 2.0f))
        SKIP("cannot initialise TextService at default+2 size");

    AppConfig config;
    config.font_size = TextService::DEFAULT_POINT_SIZE + 2.0f;

    int save_count = 0;
    GuiActionHandler handler(make_deps(ts, config, [&save_count]() { ++save_count; }));

    handler.execute("font_reset");

    INFO("in-memory font_size should be reset to DEFAULT_POINT_SIZE by font_reset");
    REQUIRE(config.font_size == TextService::DEFAULT_POINT_SIZE);
    INFO("on_config_changed should fire exactly once for font_reset");
    REQUIRE(save_count == 1);
}

// ------------------------------------------------------------------
// Test F: open_file_dialog action invokes the on_open_file_dialog callback
// ------------------------------------------------------------------
TEST_CASE("gui action handler: open_file_dialog action invokes on_open_file_dialog callback", "[config]")
{
    TextService ts;
    AppConfig config;
    GuiActionHandler::Deps deps;
    deps.text_service = &ts;
    deps.config = &config;
    int dialog_count = 0;
    deps.on_open_file_dialog = [&dialog_count]() { ++dialog_count; };
    GuiActionHandler handler(std::move(deps));

    const bool handled = handler.execute("open_file_dialog");

    INFO("open_file_dialog action should be recognised (returns true)");
    REQUIRE(handled);
    INFO("on_open_file_dialog should be called exactly once");
    REQUIRE(dialog_count == 1);
}

TEST_CASE("gui action handler: open_file_dialog does not crash when callback is null", "[config]")
{
    TextService ts;
    AppConfig config;
    GuiActionHandler::Deps deps;
    deps.text_service = &ts;
    deps.config = &config;
    // on_open_file_dialog intentionally left null
    GuiActionHandler handler(std::move(deps));

    const bool handled = handler.execute("open_file_dialog");
    INFO("open_file_dialog action should still return true with null callback");
    REQUIRE(handled);
}

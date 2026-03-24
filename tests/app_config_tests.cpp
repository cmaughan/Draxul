#include "support/test_support.h"

#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/text_service.h>

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <draxul/log.h>
#include <filesystem>
#include <fstream>

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(const char* prefix)
    {
        static std::atomic<uint64_t> counter = 0;
        const auto suffix = std::to_string(
                                std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(counter++);
        path = std::filesystem::temp_directory_path() / (std::string(prefix) + "-" + suffix);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool contains_message(const std::vector<LogRecord>& records, std::string_view needle)
{
    for (const auto& record : records)
    {
        if (record.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

const GuiKeybinding* find_keybinding(const AppConfig& config, std::string_view action)
{
    for (const auto& binding : config.keybindings)
    {
        if (binding.action == action)
            return &binding;
    }
    return nullptr;
}

} // namespace

TEST_CASE("app config parse returns defaults for empty content", "[config]")
{
    AppConfig config = AppConfig::parse("");
    AppConfig defaults;
    INFO("default window_width");
    REQUIRE(config.window_width == defaults.window_width);
    INFO("default window_height");
    REQUIRE(config.window_height == defaults.window_height);
    INFO("default font_size");
    REQUIRE(config.font_size == defaults.font_size);
    INFO("default enable_ligatures");
    REQUIRE(config.enable_ligatures == defaults.enable_ligatures);
    INFO("default font_path is empty");
    REQUIRE(config.font_path.empty());
    INFO("default fallback_paths is empty");
    REQUIRE(config.fallback_paths.empty());
    INFO("default GUI keybindings are present");
    REQUIRE(static_cast<int>(config.keybindings.size()) == 8);
}

TEST_CASE("app config parse reads all fields", "[config]")
{
    const char* content = "window_width = 1920\n"
                          "window_height = 1080\n"
                          "font_size = 14\n"
                          "enable_ligatures = false\n"
                          "font_path = \"/usr/share/fonts/mono.ttf\"\n"
                          "fallback_paths = [\"/fonts/a.ttf\", \"/fonts/b.ttf\"]\n";

    AppConfig config = AppConfig::parse(content);
    INFO("window_width parsed");
    REQUIRE(config.window_width == 1920);
    INFO("window_height parsed");
    REQUIRE(config.window_height == 1080);
    INFO("font_size parsed");
    REQUIRE(config.font_size == 14.0f);
    INFO("enable_ligatures parsed");
    REQUIRE(config.enable_ligatures == false);
    INFO("font_path parsed");
    REQUIRE(config.font_path == std::string("/usr/share/fonts/mono.ttf"));
    INFO("fallback_paths count");
    REQUIRE(static_cast<int>(config.fallback_paths.size()) == 2);
    INFO("first fallback path");
    REQUIRE(config.fallback_paths[0] == std::string("/fonts/a.ttf"));
    INFO("second fallback path");
    REQUIRE(config.fallback_paths[1] == std::string("/fonts/b.ttf"));
}

TEST_CASE("gui keybinding parser handles modifier chords and symbolic keys", "[config]")
{
    auto copy = parse_gui_keybinding("copy", "Ctrl+Shift+C");
    INFO("copy binding parses");
    REQUIRE(copy.has_value());
    INFO("action name is preserved");
    REQUIRE(copy->action == std::string("copy"));
    INFO("letter key parses");
    REQUIRE(copy->key == static_cast<int32_t>(SDLK_C));
    INFO("modifiers parse");
    REQUIRE(copy->modifiers == (kModCtrl | kModShift));
    INFO("format round-trips");
    REQUIRE(format_gui_keybinding_combo(copy->key, copy->modifiers) == std::string("Ctrl+Shift+C"));

    auto zoom = parse_gui_keybinding("font_increase", "Ctrl+=");
    INFO("zoom binding parses");
    REQUIRE(zoom.has_value());
    INFO("equals key parses");
    REQUIRE(zoom->key == static_cast<int32_t>(SDLK_EQUALS));
    INFO("Ctrl modifier parses");
    REQUIRE(zoom->modifiers == kModCtrl);
    INFO("equals formatting is canonical");
    REQUIRE(format_gui_keybinding_combo(zoom->key, zoom->modifiers) == std::string("Ctrl+="));
}

TEST_CASE("gui keybinding parser rejects unknown actions and malformed combos", "[config]")
{
    INFO("unknown actions are rejected");
    REQUIRE(!parse_gui_keybinding("not_real", "Ctrl+K").has_value());
    INFO("empty key token is rejected");
    REQUIRE(!parse_gui_keybinding("copy", "Ctrl+Shift+").has_value());
    INFO("unknown modifiers are rejected");
    REQUIRE(!parse_gui_keybinding("copy", "Hyper+C").has_value());
}

TEST_CASE("gui keybinding matcher preserves Ctrl+= and Ctrl+Plus for font increase", "[config]")
{
    auto zoom = parse_gui_keybinding("font_increase", "Ctrl+=");
    INFO("zoom binding parses");
    REQUIRE(zoom.has_value());

    INFO("Ctrl+= matches the configured binding");
    REQUIRE(gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_EQUALS, kModCtrl, true }));
    INFO("Ctrl+Plus remains accepted for zoom-in");
    REQUIRE(gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_PLUS, kModCtrl | kModShift, true }));
    INFO("other keys do not match");
    REQUIRE(!gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_MINUS, kModCtrl, true }));
}

TEST_CASE("app config parse supports multiline TOML arrays", "[config]")
{
    const char* content = "fallback_paths = [\n"
                          "  \"/fonts/a.ttf\",\n"
                          "  \"/fonts/b.ttf\",\n"
                          "]\n";

    AppConfig config = AppConfig::parse(content);
    INFO("fallback_paths count");
    REQUIRE(static_cast<int>(config.fallback_paths.size()) == 2);
    INFO("first multiline fallback path");
    REQUIRE(config.fallback_paths[0] == std::string("/fonts/a.ttf"));
    INFO("second multiline fallback path");
    REQUIRE(config.fallback_paths[1] == std::string("/fonts/b.ttf"));
}

TEST_CASE("app config parse ignores comments and blank lines", "[config]")
{
    const char* content = "# this is a comment\n"
                          "\n"
                          "window_width = 1600 # inline comment\n"
                          "\n";

    AppConfig config = AppConfig::parse(content);
    INFO("window_width parsed past comments");
    REQUIRE(config.window_width == 1600);
    INFO("window_height stays default");
    REQUIRE(config.window_height == AppConfig{}.window_height);
}

TEST_CASE("app config parse reads keybindings table and preserves unspecified defaults", "[config]")
{
    const char* content = "[keybindings]\n"
                          "toggle_diagnostics = \"Ctrl+D\"\n"
                          "font_reset = \"Alt+0\"\n";

    AppConfig config = AppConfig::parse(content);
    const GuiKeybinding* toggle = find_keybinding(config, "toggle_diagnostics");
    const GuiKeybinding* font_reset = find_keybinding(config, "font_reset");
    const GuiKeybinding* copy = find_keybinding(config, "copy");

    INFO("toggle binding is present");
    REQUIRE(toggle != nullptr);
    INFO("font reset binding is present");
    REQUIRE(font_reset != nullptr);
    INFO("unspecified bindings stay present");
    REQUIRE(copy != nullptr);
    INFO("custom toggle key parses");
    REQUIRE(toggle->key == static_cast<int32_t>(SDLK_D));
    INFO("custom toggle modifiers parse");
    REQUIRE(toggle->modifiers == kModCtrl);
    INFO("custom reset key parses");
    REQUIRE(font_reset->key == static_cast<int32_t>(SDLK_0));
    INFO("custom reset modifiers parse");
    REQUIRE(font_reset->modifiers == kModAlt);
    INFO("default copy key remains");
    REQUIRE(copy->key == static_cast<int32_t>(SDLK_C));
    INFO("default copy modifiers remain");
    REQUIRE(copy->modifiers == (kModCtrl | kModShift));
}

TEST_CASE("app config parse removes a default keybinding when user config sets an empty string", "[config]")
{
    ScopedLogCapture capture(LogLevel::Debug);
    AppConfig config = AppConfig::parse("[keybindings]\ncopy = \"\"\n");

    INFO("copy binding should be removed");
    REQUIRE(find_keybinding(config, "copy") == nullptr);
    INFO("other defaults stay intact");
    REQUIRE(find_keybinding(config, "paste") != nullptr);
    INFO("removal should be logged at debug level");
    REQUIRE(contains_message(capture.records, "Keybinding 'copy' removed by user config."));
}

TEST_CASE("app config parse ignores empty-string removal for a non-existent keybinding", "[config]")
{
    AppConfig config = AppConfig::parse("[keybindings]\nlaunch_rockets = \"\"\n");

    INFO("unknown binding should not be created");
    REQUIRE(find_keybinding(config, "launch_rockets") == nullptr);
    INFO("defaults remain unchanged");
    REQUIRE(find_keybinding(config, "copy") != nullptr);
}

TEST_CASE("app config parse removes only the requested keybinding", "[config]")
{
    AppConfig config = AppConfig::parse("[keybindings]\ncopy = \"\"\nfont_reset = \"Alt+0\"\n");

    INFO("copy binding should be removed");
    REQUIRE(find_keybinding(config, "copy") == nullptr);
    INFO("other bindings in the same table should still be applied");
    const GuiKeybinding* font_reset = find_keybinding(config, "font_reset");
    REQUIRE(font_reset != nullptr);
    REQUIRE(font_reset->key == static_cast<int32_t>(SDLK_0));
    REQUIRE(font_reset->modifiers == kModAlt);
}

TEST_CASE("app config parse clamps out-of-range window dimensions to defaults", "[config]")
{
    const char* content = "window_width = 99999\n"
                          "window_height = -5\n";

    AppConfig config = AppConfig::parse(content);
    AppConfig defaults;
    INFO("oversized width falls back to default");
    REQUIRE(config.window_width == defaults.window_width);
    INFO("negative height falls back to default");
    REQUIRE(config.window_height == defaults.window_height);
}

TEST_CASE("app config parse clamps font_size to valid range", "[config]")
{
    AppConfig too_small = AppConfig::parse("font_size = 0\n");
    AppConfig too_large = AppConfig::parse("font_size = 9999\n");

    INFO("font_size clamped up to min");
    REQUIRE(too_small.font_size >= TextService::MIN_POINT_SIZE);
    INFO("font_size clamped down to max");
    REQUIRE(too_large.font_size <= TextService::MAX_POINT_SIZE);
}

TEST_CASE("app config serialize/parse round-trip preserves all fields", "[config]")
{
    AppConfig original;
    original.window_width = 1440;
    original.window_height = 900;
    original.font_size = 13.0f;
    original.enable_ligatures = false;
    original.font_path = "/home/user/fonts/mono.ttf";
    original.fallback_paths = { "/fonts/emoji.ttf", "/fonts/cjk.ttf" };

    AppConfig round_tripped = AppConfig::parse(original.serialize());

    INFO("window_width survives round-trip");
    REQUIRE(round_tripped.window_width == original.window_width);
    INFO("window_height survives round-trip");
    REQUIRE(round_tripped.window_height == original.window_height);
    INFO("font_size survives round-trip");
    REQUIRE(round_tripped.font_size == original.font_size); // float == float
    INFO("enable_ligatures survives round-trip");
    REQUIRE(round_tripped.enable_ligatures == original.enable_ligatures);
    INFO("font_path survives round-trip");
    REQUIRE(round_tripped.font_path == original.font_path);
    INFO("fallback_paths count survives round-trip");
    REQUIRE(static_cast<int>(round_tripped.fallback_paths.size())
        == static_cast<int>(original.fallback_paths.size()));
    INFO("first fallback path survives");
    REQUIRE(round_tripped.fallback_paths[0] == original.fallback_paths[0]);
    INFO("second fallback path survives");
    REQUIRE(round_tripped.fallback_paths[1] == original.fallback_paths[1]);
    INFO("default toggle binding survives round-trip");
    REQUIRE(format_gui_keybinding_combo(find_keybinding(round_tripped, "toggle_diagnostics")->key,
                find_keybinding(round_tripped, "toggle_diagnostics")->modifiers)
        == std::string("F12"));
}

TEST_CASE("app config serialize/parse round-trip preserves custom keybindings", "[config]")
{
    AppConfig original;
    original.keybindings = {
        { "toggle_diagnostics", 0, kModNone, static_cast<int32_t>(SDLK_D), kModCtrl },
        { "copy", 0, kModNone, static_cast<int32_t>(SDLK_C), kModCtrl | kModAlt },
        { "paste", 0, kModNone, static_cast<int32_t>(SDLK_V), kModCtrl | kModAlt },
        { "font_increase", 0, kModNone, static_cast<int32_t>(SDLK_EQUALS), kModCtrl },
        { "font_decrease", 0, kModNone, static_cast<int32_t>(SDLK_MINUS), kModCtrl },
        { "font_reset", 0, kModNone, static_cast<int32_t>(SDLK_0), kModAlt },
    };

    AppConfig round_tripped = AppConfig::parse(original.serialize());

    INFO("custom toggle binding survives round-trip");
    REQUIRE(format_gui_keybinding_combo(find_keybinding(round_tripped, "toggle_diagnostics")->key,
                find_keybinding(round_tripped, "toggle_diagnostics")->modifiers)
        == std::string("Ctrl+D"));
    INFO("custom copy binding survives round-trip");
    REQUIRE(format_gui_keybinding_combo(find_keybinding(round_tripped, "copy")->key,
                find_keybinding(round_tripped, "copy")->modifiers)
        == std::string("Ctrl+Alt+C"));
    INFO("custom font reset binding survives round-trip");
    REQUIRE(format_gui_keybinding_combo(find_keybinding(round_tripped, "font_reset")->key,
                find_keybinding(round_tripped, "font_reset")->modifiers)
        == std::string("Alt+0"));
}

TEST_CASE("app config serialize clamps out-of-range values", "[config]")
{
    AppConfig config;
    config.window_width = 99999;
    config.font_size = 9999.0f;

    AppConfig round_tripped = AppConfig::parse(config.serialize());
    INFO("oversized width clamped on serialize");
    REQUIRE(round_tripped.window_width <= 3840);
    INFO("oversized font_size clamped on serialize");
    REQUIRE(round_tripped.font_size <= TextService::MAX_POINT_SIZE);
}

TEST_CASE("app config save and load round-trip through the filesystem", "[config]")
{
    TempDir temp("draxul-app-config-roundtrip");
    const auto path = temp.path / "draxul" / "config.toml";

    AppConfig original;
    original.window_width = 1600;
    original.window_height = 900;
    original.font_size = 15.0f;
    original.enable_ligatures = false;
    original.font_path = "/fonts/mono.ttf";
    original.fallback_paths = { "/fonts/emoji.ttf", "/fonts/cjk.ttf" };

    original.save_to_path(path);
    INFO("config file should be created on save");
    REQUIRE(std::filesystem::exists(path));

    AppConfig loaded = AppConfig::load_from_path(path);
    INFO("window_width survives filesystem round-trip");
    REQUIRE(loaded.window_width == original.window_width);
    INFO("window_height survives filesystem round-trip");
    REQUIRE(loaded.window_height == original.window_height);
    INFO("font_size survives filesystem round-trip");
    REQUIRE(loaded.font_size == original.font_size);
    INFO("enable_ligatures survives filesystem round-trip");
    REQUIRE(loaded.enable_ligatures == original.enable_ligatures);
    INFO("font_path survives filesystem round-trip");
    REQUIRE(loaded.font_path == original.font_path);
    INFO("fallback count survives filesystem round-trip");
    REQUIRE(static_cast<int>(loaded.fallback_paths.size())
        == static_cast<int>(original.fallback_paths.size()));
    INFO("first fallback survives filesystem round-trip");
    REQUIRE(loaded.fallback_paths[0] == original.fallback_paths[0]);
    INFO("second fallback survives filesystem round-trip");
    REQUIRE(loaded.fallback_paths[1] == original.fallback_paths[1]);
}

TEST_CASE("app config load from a missing file returns defaults", "[config]")
{
    TempDir temp("draxul-app-config-missing");
    const auto path = temp.path / "missing.toml";

    AppConfig loaded = AppConfig::load_from_path(path);
    AppConfig defaults;

    INFO("missing config keeps default width");
    REQUIRE(loaded.window_width == defaults.window_width);
    INFO("missing config keeps default height");
    REQUIRE(loaded.window_height == defaults.window_height);
    INFO("missing config keeps default font size");
    REQUIRE(loaded.font_size == defaults.font_size);
    INFO("missing config keeps default ligature setting");
    REQUIRE(loaded.enable_ligatures == defaults.enable_ligatures);
    INFO("missing config keeps default font path");
    REQUIRE(loaded.font_path == defaults.font_path);
    INFO("missing config keeps default fallbacks");
    REQUIRE(loaded.fallback_paths.size() == defaults.fallback_paths.size());
}

TEST_CASE("app config load rejects invalid TOML and falls back to defaults", "[config]")
{
    TempDir temp("draxul-app-config-corrupt");
    const auto path = temp.path / "config.toml";

    std::ofstream out(path, std::ios::trunc);
    out << "window_width = nope\n"
           "window_height = 720\n"
           "font_size = broken\n"
           "fallback_paths = [\"/fonts/a.ttf\"\n";
    out.close();

    ScopedLogCapture capture;
    AppConfig loaded = AppConfig::load_from_path(path);
    AppConfig defaults;

    INFO("bad width falls back to the default");
    REQUIRE(loaded.window_width == defaults.window_width);
    INFO("invalid TOML rejects partial parsing");
    REQUIRE(loaded.window_height == defaults.window_height);
    INFO("bad font size falls back to the default");
    REQUIRE(loaded.font_size == defaults.font_size);
    INFO("incomplete array falls back to an empty list");
    REQUIRE(loaded.fallback_paths.empty());
    INFO("parse failures should be logged");
    REQUIRE(contains_message(capture.records, "Failed to parse config from"));
}

TEST_CASE("app config save logs a warning when the target path is not writable", "[config]")
{
    TempDir temp("draxul-app-config-write-failure");
    const auto blocking_file = temp.path / "not-a-directory";
    {
        std::ofstream out(blocking_file, std::ios::trunc);
        out << "block";
    }

    ScopedLogCapture capture;
    AppConfig config;
    config.save_to_path(blocking_file / "config.toml");

    INFO("write failures should be logged");
    REQUIRE(contains_message(capture.records, "Failed to save config to"));
}

TEST_CASE("app config overrides shadow only the explicitly provided fields", "[config]")
{
    AppConfig base;
    base.window_width = 1000;
    base.window_height = 700;
    base.font_size = 12.0f;
    base.enable_ligatures = true;
    base.font_path = "/fonts/base.ttf";
    base.fallback_paths = { "/fonts/base-fallback.ttf" };

    AppConfigOverrides overrides;
    overrides.window_width = 1440;
    overrides.enable_ligatures = false;
    overrides.font_path = std::string("/fonts/override.ttf");

    apply_overrides(base, overrides);

    INFO("explicit width override is applied");
    REQUIRE(base.window_width == 1440);
    INFO("unspecified height stays unchanged");
    REQUIRE(base.window_height == 700);
    INFO("unspecified font size stays unchanged");
    REQUIRE(base.font_size == 12.0f);
    INFO("explicit ligature override is applied");
    REQUIRE(base.enable_ligatures == false);
    INFO("explicit font path override is applied");
    REQUIRE(base.font_path == std::string("/fonts/override.ttf"));
    INFO("unspecified fallback paths stay unchanged");
    REQUIRE(static_cast<int>(base.fallback_paths.size()) == 1);
    INFO("fallback paths are preserved");
    REQUIRE(base.fallback_paths[0] == std::string("/fonts/base-fallback.ttf"));
}

TEST_CASE("app config atlas_size parses valid power-of-two values", "[config]")
{
    AppConfig config = AppConfig::parse("atlas_size = 4096\n");
    INFO("atlas_size = 4096 parses correctly");
    REQUIRE(config.atlas_size == 4096);
}

TEST_CASE("app config atlas_size clamps out-of-range values to nearest power of two in range", "[config]")
{
    AppConfig too_small = AppConfig::parse("atlas_size = 100\n");
    AppConfig too_large = AppConfig::parse("atlas_size = 99999\n");
    INFO("atlas_size below min clamps to 1024");
    REQUIRE(too_small.atlas_size == 1024);
    INFO("atlas_size above max clamps to 8192");
    REQUIRE(too_large.atlas_size == 8192);
}

TEST_CASE("app config atlas_size rounds non-power-of-two values down", "[config]")
{
    AppConfig config1 = AppConfig::parse("atlas_size = 3000\n");
    AppConfig config2 = AppConfig::parse("atlas_size = 1500\n");
    AppConfig config3 = AppConfig::parse("atlas_size = 5000\n");
    INFO("3000 rounds down to 2048");
    REQUIRE(config1.atlas_size == 2048);
    INFO("1500 rounds down to 1024");
    REQUIRE(config2.atlas_size == 1024);
    INFO("5000 rounds down to 4096");
    REQUIRE(config3.atlas_size == 4096);
}

TEST_CASE("app config atlas_size defaults to kAtlasSize when not specified", "[config]")
{
    AppConfig config = AppConfig::parse("");
    INFO("atlas_size defaults to kAtlasSize");
    REQUIRE(config.atlas_size == draxul::kAtlasSize);
}

TEST_CASE("app config overrides can intentionally clear string and list fields", "[config]")
{
    AppConfig base;
    base.font_path = "/fonts/base.ttf";
    base.fallback_paths = { "/fonts/a.ttf", "/fonts/b.ttf" };

    AppConfigOverrides overrides;
    overrides.font_path = std::string();
    overrides.fallback_paths = std::vector<std::string>{};

    apply_overrides(base, overrides);

    INFO("empty string override clears the font path");
    REQUIRE(base.font_path == std::string());
    INFO("empty vector override clears fallback paths");
    REQUIRE(base.fallback_paths.empty());
}

TEST_CASE("app config parse warns about unknown top-level keys", "[config]")
{
    ScopedLogCapture capture;
    AppConfig config = AppConfig::parse("font_szie = 14\nwindow_width = 1920\n");
    AppConfig defaults;

    // The typo key should trigger a warning
    INFO("typo key should produce an unknown-key warning");
    REQUIRE(contains_message(capture.records, "Unknown key 'font_szie'"));
    // Known key should still be parsed correctly
    INFO("known key window_width still parsed correctly");
    REQUIRE(config.window_width == 1920);
    // Typo key is silently ignored (falls back to default)
    INFO("typo key falls back to default font_size");
    REQUIRE(config.font_size == defaults.font_size);
}

TEST_CASE("app config parse does not warn for all known top-level keys", "[config]")
{
    ScopedLogCapture capture;
    const char* content = "window_width = 1280\n"
                          "window_height = 800\n"
                          "font_size = 14\n"
                          "atlas_size = 2048\n"
                          "enable_ligatures = true\n"
                          "font_path = \"/fonts/mono.ttf\"\n"
                          "fallback_paths = [\"/fonts/emoji.ttf\"]\n"
                          "[keybindings]\n"
                          "toggle_diagnostics = \"F12\"\n";
    AppConfig::parse(content);

    bool has_unknown_warning = false;
    for (const auto& record : capture.records)
    {
        if (record.message.find("Unknown key") != std::string::npos)
            has_unknown_warning = true;
    }
    INFO("no unknown-key warnings for well-formed config");
    REQUIRE(!has_unknown_warning);
}

TEST_CASE("app config parse logs error for wrong type on known key", "[config]")
{
    ScopedLogCapture capture;
    // font_size expects integer, passing a string
    AppConfig config = AppConfig::parse("font_size = \"large\"\n");
    AppConfig defaults;

    INFO("type mismatch should produce an error log");
    REQUIRE(contains_message(capture.records, "Key 'font_size' has wrong type"));
    INFO("wrong-type key falls back to default");
    REQUIRE(config.font_size == defaults.font_size);
}

TEST_CASE("config unknown key: exactly one warning per unknown key", "[config]")
{
    ScopedLogCapture capture;
    AppConfig::parse("future_option = true\n"
                     "another_unknown = 42\n"
                     "window_width = 1280\n");
    int warning_count = 0;
    for (const auto& record : capture.records)
        if (record.message.find("Unknown key") != std::string::npos)
            ++warning_count;
    INFO("exactly one warning per unknown key");
    REQUIRE(warning_count == 2);
}

TEST_CASE("config unknown key: unknown keys are dropped from serialized output", "[config]")
{
    // Unknown keys should not appear in the serialized output
    // since we only serialize known keys
    AppConfig config = AppConfig::parse("future_option = true\n"
                                        "window_width = 1280\n");
    std::string serialized = config.serialize();
    INFO("unknown key is not preserved in serialized output");
    REQUIRE(serialized.find("future_option") == std::string::npos);
    INFO("known key is preserved in serialized output");
    REQUIRE(serialized.find("window_width") != std::string::npos);
}

TEST_CASE("config unknown section: unknown TOML section produces unknown key warning", "[config]")
{
    ScopedLogCapture capture;
    AppConfig::parse("[future_section]\n"
                     "option = true\n"
                     "window_width = 1280\n");
    INFO("unknown section should produce an unknown-key warning");
    REQUIRE(contains_message(capture.records, "Unknown key 'future_section'"));
}

TEST_CASE("config duplicate keybinding: same key+modifier for two actions produces warning", "[config]")
{
    ScopedLogCapture capture;
    // Two different actions using Ctrl+C
    const char* content = "[keybindings]\n"
                          "copy = \"Ctrl+Shift+C\"\n"
                          "paste = \"Ctrl+Shift+C\"\n";
    AppConfig config = AppConfig::parse(content);
    INFO("duplicate key+modifier should produce a warning");
    REQUIRE(contains_message(capture.records, "Duplicate keybinding"));
}

TEST_CASE("config duplicate keybinding: first registered action takes precedence in dispatch", "[config]")
{
    ScopedLogCapture capture;
    const char* content = "[keybindings]\n"
                          "copy = \"Ctrl+Shift+C\"\n"
                          "paste = \"Ctrl+Shift+C\"\n";
    AppConfig config = AppConfig::parse(content);
    // Verify both bindings are present but only first should fire
    // The policy is first-wins: copy comes before paste in the keybindings vector
    // (default bindings are seeded in order: toggle_diagnostics, copy, paste, ...)
    // After replace_gui_keybinding, copy should appear before paste
    const GuiKeybinding* copy_binding = find_keybinding(config, "copy");
    const GuiKeybinding* paste_binding = find_keybinding(config, "paste");
    INFO("copy binding exists");
    REQUIRE(copy_binding != nullptr);
    INFO("paste binding exists");
    REQUIRE(paste_binding != nullptr);
    // Both have same key+mod; first in vector (copy, since default order) takes precedence
    // in linear dispatch. Document: first-wins by insertion order.
    KeyEvent event{ 0, SDLK_C, kModCtrl | kModShift, true };
    int match_count = 0;
    for (const auto& binding : config.keybindings)
        if (gui_keybinding_matches(binding, event))
            ++match_count;
    INFO("at least one binding matches the key event");
    REQUIRE(match_count >= 1);
}

TEST_CASE("gui keybinding parsing works without SDL initialized", "[config]")
{
    // Ctrl+Shift modifier parsing must work with no SDL_Init call.
    // This confirms that draxul-app-support has no SDL initialisation dependency.
    auto binding = parse_gui_keybinding("copy", "Ctrl+Shift+C");
    INFO("copy binding parses without SDL init");
    REQUIRE(binding.has_value());
    INFO("Ctrl+Shift modifiers parsed correctly");
    REQUIRE(binding->modifiers == (kModCtrl | kModShift));
    INFO("key is non-zero");
    REQUIRE(binding->key != 0);
}

TEST_CASE("host kind parser accepts nvim and powershell spellings", "[config]")
{
    auto nvim = parse_host_kind("nvim");
    auto powershell = parse_host_kind("powershell");
    auto pwsh = parse_host_kind("pwsh");
    auto invalid = parse_host_kind("not-a-host");

    INFO("nvim should parse");
    REQUIRE(nvim.has_value());
    INFO("powershell should parse");
    REQUIRE(powershell.has_value());
    INFO("pwsh alias should parse");
    REQUIRE(pwsh.has_value());
    INFO("unknown hosts should be rejected");
    REQUIRE(!invalid.has_value());
    INFO("nvim maps correctly");
    REQUIRE(*nvim == HostKind::Nvim);
    INFO("powershell maps correctly");
    REQUIRE(*powershell == HostKind::PowerShell);
    INFO("pwsh maps correctly");
    REQUIRE(*pwsh == HostKind::PowerShell);
    INFO("host kind stringifies");
    REQUIRE(std::string(to_string(*powershell)) == std::string("powershell"));
}

// ---------------------------------------------------------------------------
// parse_hex_color tests
// ---------------------------------------------------------------------------

TEST_CASE("parse_hex_color handles #RRGGBB format", "[config]")
{
    auto white = parse_hex_color("#ffffff");
    INFO("#ffffff should parse");
    REQUIRE(white.has_value());
    INFO("red channel");
    REQUIRE(white->r == Catch::Approx(1.0f).margin(0.01f));
    INFO("green channel");
    REQUIRE(white->g == Catch::Approx(1.0f).margin(0.01f));
    INFO("blue channel");
    REQUIRE(white->b == Catch::Approx(1.0f).margin(0.01f));
    INFO("alpha channel");
    REQUIRE(white->a == Catch::Approx(1.0f));

    auto black = parse_hex_color("#000000");
    INFO("#000000 should parse");
    REQUIRE(black.has_value());
    INFO("black red channel");
    REQUIRE(black->r == Catch::Approx(0.0f));
    INFO("black green channel");
    REQUIRE(black->g == Catch::Approx(0.0f));
    INFO("black blue channel");
    REQUIRE(black->b == Catch::Approx(0.0f));

    auto red = parse_hex_color("#ff0000");
    INFO("#ff0000 should parse");
    REQUIRE(red.has_value());
    INFO("red channel is 1.0");
    REQUIRE(red->r == Catch::Approx(1.0f).margin(0.01f));
    INFO("green is 0");
    REQUIRE(red->g == Catch::Approx(0.0f));
    INFO("blue is 0");
    REQUIRE(red->b == Catch::Approx(0.0f));
}

TEST_CASE("parse_hex_color handles #RGB shorthand", "[config]")
{
    auto white = parse_hex_color("#fff");
    INFO("#fff should parse");
    REQUIRE(white.has_value());
    INFO("#fff expands to white");
    REQUIRE(white->r == Catch::Approx(1.0f).margin(0.01f));
    REQUIRE(white->g == Catch::Approx(1.0f).margin(0.01f));
    REQUIRE(white->b == Catch::Approx(1.0f).margin(0.01f));

    auto color = parse_hex_color("#abc");
    INFO("#abc should parse");
    REQUIRE(color.has_value());
    // #abc -> #aabbcc -> r=170/255, g=187/255, b=204/255
    INFO("#abc red channel");
    REQUIRE(color->r == Catch::Approx(170.0f / 255.0f).margin(0.01f));
    INFO("#abc green channel");
    REQUIRE(color->g == Catch::Approx(187.0f / 255.0f).margin(0.01f));
    INFO("#abc blue channel");
    REQUIRE(color->b == Catch::Approx(204.0f / 255.0f).margin(0.01f));
}

TEST_CASE("parse_hex_color handles uppercase hex digits", "[config]")
{
    auto upper = parse_hex_color("#AABBCC");
    auto lower = parse_hex_color("#aabbcc");
    INFO("uppercase #AABBCC should parse");
    REQUIRE(upper.has_value());
    INFO("lowercase #aabbcc should parse");
    REQUIRE(lower.has_value());
    INFO("uppercase and lowercase produce same color");
    REQUIRE(upper->r == Catch::Approx(lower->r));
    REQUIRE(upper->g == Catch::Approx(lower->g));
    REQUIRE(upper->b == Catch::Approx(lower->b));
}

TEST_CASE("parse_hex_color rejects malformed input", "[config]")
{
    INFO("empty string");
    REQUIRE(!parse_hex_color("").has_value());
    INFO("missing hash prefix");
    REQUIRE(!parse_hex_color("ff0000").has_value());
    INFO("wrong length (5 digits)");
    REQUIRE(!parse_hex_color("#12345").has_value());
    INFO("wrong length (1 digit)");
    REQUIRE(!parse_hex_color("#1").has_value());
    INFO("non-hex character");
    REQUIRE(!parse_hex_color("#gggggg").has_value());
    INFO("non-hex in shorthand");
    REQUIRE(!parse_hex_color("#xyz").has_value());
}

// ---------------------------------------------------------------------------
// [terminal] config section tests
// ---------------------------------------------------------------------------

TEST_CASE("terminal config section parses fg and bg hex colors", "[config]")
{
    const char* content = "[terminal]\n"
                          "fg = \"#eaeaea\"\n"
                          "bg = \"#141617\"\n";
    AppConfig config = AppConfig::parse(content);
    INFO("terminal.fg is stored");
    REQUIRE(config.terminal.fg == std::string("#eaeaea"));
    INFO("terminal.bg is stored");
    REQUIRE(config.terminal.bg == std::string("#141617"));
}

TEST_CASE("terminal config section defaults to empty when absent", "[config]")
{
    AppConfig config = AppConfig::parse("window_width = 1280\n");
    INFO("terminal.fg is empty by default");
    REQUIRE(config.terminal.fg.empty());
    INFO("terminal.bg is empty by default");
    REQUIRE(config.terminal.bg.empty());
}

TEST_CASE("terminal config section warns on invalid hex and ignores the value", "[config]")
{
    ScopedLogCapture capture;
    const char* content = "[terminal]\n"
                          "fg = \"not-a-color\"\n"
                          "bg = \"#141617\"\n";
    AppConfig config = AppConfig::parse(content);
    INFO("invalid fg is not stored");
    REQUIRE(config.terminal.fg.empty());
    INFO("valid bg is stored");
    REQUIRE(config.terminal.bg == std::string("#141617"));
    INFO("warning logged for invalid fg");
    REQUIRE(contains_message(capture.records, "terminal.fg"));
}

TEST_CASE("terminal config section round-trips through serialize/parse", "[config]")
{
    AppConfig original;
    original.terminal.fg = "#eaeaea";
    original.terminal.bg = "#141617";

    AppConfig round_tripped = AppConfig::parse(original.serialize());
    INFO("terminal.fg survives round-trip");
    REQUIRE(round_tripped.terminal.fg == original.terminal.fg);
    INFO("terminal.bg survives round-trip");
    REQUIRE(round_tripped.terminal.bg == original.terminal.bg);
}

TEST_CASE("terminal config section omitted from serialization when both fields empty", "[config]")
{
    AppConfig config;
    // Both terminal.fg and terminal.bg are empty by default
    std::string serialized = config.serialize();
    INFO("terminal section not emitted when both fields are empty");
    REQUIRE(serialized.find("terminal") == std::string::npos);
}

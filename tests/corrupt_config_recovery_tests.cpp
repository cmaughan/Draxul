#include "support/test_support.h"

#include <draxul/app_config.h>
#include <draxul/log.h>

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

using namespace draxul;
using namespace draxul::tests;

namespace
{

// Helper: write arbitrary content to a temp config file, load it, and return both the
// config and the path (for cleanup). The caller owns the file lifetime via TempConfigFile.
struct TempConfigFile
{
    std::filesystem::path dir;
    std::filesystem::path file;

    explicit TempConfigFile(std::string_view content, const char* suffix = "config.toml")
    {
        static int counter = 0;
        dir = std::filesystem::temp_directory_path()
            / ("draxul-corrupt-test-" + std::to_string(++counter));
        std::filesystem::create_directories(dir);
        file = dir / suffix;
        std::ofstream out(file, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ~TempConfigFile()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

bool has_warn_log(const std::vector<LogRecord>& records, std::string_view needle)
{
    for (const auto& r : records)
    {
        if (r.level == LogLevel::Warn && r.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void assert_defaults(const AppConfig& config)
{
    AppConfig defaults;
    REQUIRE(config.window_width == defaults.window_width);
    REQUIRE(config.window_height == defaults.window_height);
    REQUIRE(config.font_size == defaults.font_size);
    REQUIRE(config.enable_ligatures == defaults.enable_ligatures);
    REQUIRE(config.smooth_scroll == defaults.smooth_scroll);
    REQUIRE(config.scroll_speed == defaults.scroll_speed);
    REQUIRE(config.font_path.empty());
    REQUIRE(config.fallback_paths.empty());
}

} // namespace

TEST_CASE("corrupt config: missing file returns defaults without crash", "[config][corrupt]")
{
    auto path = std::filesystem::temp_directory_path() / "draxul-nonexistent-dir" / "config.toml";
    // Ensure it truly does not exist.
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::load_from_path(path);

    assert_defaults(config);
    // No warning expected -- a missing file is a normal first-run scenario.
}

TEST_CASE("corrupt config: empty file returns defaults", "[config][corrupt]")
{
    TempConfigFile tmp("");

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::load_from_path(tmp.file);

    assert_defaults(config);
}

TEST_CASE("corrupt config: garbage binary content produces WARN and defaults", "[config][corrupt]")
{
    // Construct content that is definitely not valid TOML: null bytes, control chars, random binary.
    std::string garbage;
    garbage.resize(256);
    for (int i = 0; i < 256; ++i)
        garbage[static_cast<size_t>(i)] = static_cast<char>(i);

    TempConfigFile tmp(garbage);

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::load_from_path(tmp.file);

    assert_defaults(config);
    INFO("Expected a WARN about parse failure");
    REQUIRE(has_warn_log(capture.records, "Failed to parse config"));
}

TEST_CASE("corrupt config: parse() with garbage string returns defaults", "[config][corrupt]")
{
    // Test the in-memory parse path (no file I/O).
    std::string garbage = "\x00\x01\x02\xFF\xFEthis is not toml!!! {{{";

    AppConfig config = AppConfig::parse(garbage);
    assert_defaults(config);
}

TEST_CASE("corrupt config: unknown top-level keys produce WARN", "[config][corrupt]")
{
    const char* content = "window_width = 1280\n"
                          "totally_fake_key = 42\n"
                          "another_unknown = \"hello\"\n";

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::parse(content);

    INFO("window_width should still parse correctly");
    REQUIRE(config.window_width == 1280);

    INFO("Expected WARN for 'totally_fake_key'");
    REQUIRE(has_warn_log(capture.records, "totally_fake_key"));
    INFO("Expected WARN for 'another_unknown'");
    REQUIRE(has_warn_log(capture.records, "another_unknown"));
}

TEST_CASE("corrupt config: unknown keys via load_from_path still warn", "[config][corrupt]")
{
    TempConfigFile tmp("mystery_setting = true\n");

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::load_from_path(tmp.file);

    assert_defaults(config);
    INFO("Expected WARN for unknown key");
    REQUIRE(has_warn_log(capture.records, "mystery_setting"));
}

TEST_CASE("corrupt config: scroll_speed out of range is clamped with WARN", "[config][corrupt]")
{
    SECTION("scroll_speed too high")
    {
        const char* content = "scroll_speed = 999.0\n";

        ScopedLogCapture capture(LogLevel::Warn);
        AppConfig config = AppConfig::parse(content);

        INFO("scroll_speed should fall back to 1.0");
        REQUIRE(config.scroll_speed == 1.0f);
        INFO("Expected WARN about out-of-range scroll_speed");
        REQUIRE(has_warn_log(capture.records, "scroll_speed"));
    }

    SECTION("scroll_speed too low")
    {
        const char* content = "scroll_speed = 0.001\n";

        ScopedLogCapture capture(LogLevel::Warn);
        AppConfig config = AppConfig::parse(content);

        INFO("scroll_speed should fall back to 1.0");
        REQUIRE(config.scroll_speed == 1.0f);
        INFO("Expected WARN about out-of-range scroll_speed");
        REQUIRE(has_warn_log(capture.records, "scroll_speed"));
    }

    SECTION("scroll_speed negative")
    {
        const char* content = "scroll_speed = -5.0\n";

        ScopedLogCapture capture(LogLevel::Warn);
        AppConfig config = AppConfig::parse(content);

        REQUIRE(config.scroll_speed == 1.0f);
        REQUIRE(has_warn_log(capture.records, "scroll_speed"));
    }
}

TEST_CASE("corrupt config: valid TOML with wrong types uses defaults", "[config][corrupt]")
{
    // window_width should be integer, not string; enable_ligatures should be bool, not int.
    const char* content = "window_width = \"not a number\"\n"
                          "enable_ligatures = 42\n";

    ScopedLogCapture capture(LogLevel::Error);
    AppConfig config = AppConfig::parse(content);

    AppConfig defaults;
    INFO("window_width should fall back to default");
    REQUIRE(config.window_width == defaults.window_width);
    INFO("enable_ligatures should fall back to default");
    REQUIRE(config.enable_ligatures == defaults.enable_ligatures);
}

TEST_CASE("corrupt config: load_from_path with binary file returns defaults", "[config][corrupt]")
{
    // A realistic scenario: someone accidentally overwrites config.toml with a binary file.
    std::string binary_content;
    binary_content += '\x89'; // PNG magic bytes
    binary_content += "PNG\r\n\x1a\n";
    binary_content.append(128, '\0');

    TempConfigFile tmp(binary_content);

    ScopedLogCapture capture(LogLevel::Warn);
    AppConfig config = AppConfig::load_from_path(tmp.file);

    assert_defaults(config);
    INFO("Expected a WARN about parse failure for binary content");
    REQUIRE(has_warn_log(capture.records, "Failed to parse config"));
}

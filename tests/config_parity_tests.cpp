// Byte-exact serialization parity snapshots for the declarative config schema
// migration (kanban/done/21 declarative-config-schema -refactor.md).
//
// These tests pin the exact TOML text produced by the config layer BEFORE the
// schema migration. Every migration group must keep them byte-identical; they
// are the behavioral baseline demanded by the work item.
//
// Golden files live in tests/fixtures/config/. To regenerate after an
// INTENTIONAL format change, run:
//
//   DRAXUL_REGEN_CONFIG_GOLDENS=1 ./build/tests/draxul-test-core "[parity]"
//
// and review the fixture diff like any other code change.

#include "support/temp_dir.h"

#include <draxul/app_config.h>
#include <draxul/config_document.h>
#include <draxul/toml_support.h>

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

using namespace draxul;

namespace
{

std::filesystem::path fixture_path(const char* name)
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "tests" / "fixtures" / "config" / name;
}

bool regen_goldens()
{
    const char* env = std::getenv("DRAXUL_REGEN_CONFIG_GOLDENS");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}

std::string canonicalize_checkout_bytes(std::string text)
{
    // Git may materialize checked text fixtures as CRLF on Windows, whereas
    // AppConfig::serialize() intentionally returns a platform-neutral LF
    // string (ofstream performs native text conversion when saving). Compare
    // the canonical in-memory representation rather than checkout policy.
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    // toml++ may choose either of two equivalent last-digit spellings for a
    // double that originated as float (MSVC currently emits
    // 0.8999999761581421; libc++ has emitted 0.89999997615814209). Preserve
    // byte checks for layout, ordering, quoting, booleans, and integers while
    // canonicalizing only standalone floating-point assignment values.
    std::string result;
    result.reserve(text.size());
    size_t cursor = 0;
    while (cursor < text.size())
    {
        const size_t newline = text.find('\n', cursor);
        const size_t end = newline == std::string::npos ? text.size() : newline;
        std::string line = text.substr(cursor, end - cursor);
        const size_t equals = line.find(" = ");
        if (equals != std::string::npos)
        {
            const size_t value_start = equals + 3;
            const std::string value = line.substr(value_start);
            if (value.find_first_of(".eE") != std::string::npos)
            {
                char* parse_end = nullptr;
                const double parsed = std::strtod(value.c_str(), &parse_end);
                if (parse_end == value.c_str() + value.size())
                {
                    std::ostringstream canonical;
                    canonical << std::setprecision(std::numeric_limits<double>::max_digits10)
                              << parsed;
                    line.replace(value_start, std::string::npos, canonical.str());
                }
            }
        }
        result.append(line);
        if (newline != std::string::npos)
            result.push_back('\n');
        cursor = newline == std::string::npos ? text.size() : newline + 1;
    }
    return result;
}

void compare_against_golden(const std::string& actual, const char* golden_name)
{
    const std::filesystem::path golden = fixture_path(golden_name);
    if (regen_goldens())
    {
        std::filesystem::create_directories(golden.parent_path());
        std::ofstream out(golden, std::ios::binary | std::ios::trunc);
        out << actual;
    }

    INFO("golden fixture: " << golden.string());
    INFO("regenerate with DRAXUL_REGEN_CONFIG_GOLDENS=1 if this change is intentional");
    const std::string expected = draxul::tests::read_file(golden);
    REQUIRE_FALSE(expected.empty());

    // Semantic parity remains exact even where the formatter's equivalent
    // floating spelling differs between standard-library implementations.
    const auto actual_document = toml_support::parse_document(actual);
    const auto expected_document = toml_support::parse_document(expected);
    REQUIRE(actual_document.has_value());
    REQUIRE(expected_document.has_value());
    CHECK(*actual_document == *expected_document);

    CHECK(canonicalize_checkout_bytes(actual) == canonicalize_checkout_bytes(expected));
}

// Every config key the app owns, each set to a valid non-default value. Keep in
// sync with the schema completeness test in config_schema_tests.cpp when keys
// are added.
constexpr const char* kKitchenSinkToml = R"(window_width = 1777
window_height = 999
font_size = 17.5
space_sidebar_columns = 31
atlas_size = 4096
enable_ligatures = false
smooth_scroll = false
scroll_speed = 2.75
scrollback_lines = 4321
palette_bg_alpha = 0.42
focus_border_width = 7.0
enable_toast_notifications = false
toast_duration_s = 9.5
show_pane_status = false
chord_timeout_ms = 1234
chord_indicator_fade_ms = 2345
weather_location = "York, UK"
font_path = "/fonts/regular.ttf"
bold_font_path = "/fonts/bold.ttf"
italic_font_path = "/fonts/italic.ttf"
bold_italic_font_path = "/fonts/bold-italic.ttf"
fallback_paths = ["/fonts/emoji.ttf", "/fonts/cjk.ttf"]

[keybindings]
copy = "Ctrl+Alt+X"
toggle_copy_mode = "Ctrl+S, Return"
split_vertical = "Ctrl+S, |"

[terminal]
fg = "#102030"
bg = "#405060"
selection_max_cells = 12345
copy_on_select = false
paste_confirm_lines = 12
url_detection = false
enable_osc8_hyperlinks = false
enable_shell_integration_marks = false

[markdown]
font_size = 15.5
margin_columns = 4.5

[chrome]
tab_bar_bg = "#111213"
tab_active_fg = "#141516"
tab_inactive_fg = "#171819"
space_active_bg = "#18191a"
agent_active_bg = "#191a1b"
tab_active_bg = "#1a1b1c"
tab_inactive_bg = "#1d1e1f"
tab_editing_bg = "#202122"
divider = "#232425"
focus_border = "#262728"
status_bar_bg = "#292a2b"
status_bar_fg = "#2c2d2e"
status_focused_accent_bg = "#2f3031"
status_inactive_accent_bg = "#323334"
status_editing_bg = "#353637"
resource_pill_bg = "#38393a"
resource_pill_fg = "#3b3c3d"
resource_pill_warn_bg = "#3e3f40"
resource_pill_hot_bg = "#414243"
chord_pill_bg = "#444546"
weather_pill_bg = "#474849"
editing_outline = "#4a4b4c"
)";

// A user config document with comments, formatting quirks, and module-owned
// tables. merge_core_config must keep the module tables verbatim while
// replacing app-owned keys.
constexpr const char* kUserDocumentToml = R"(# Draxul user config
window_width = 1200 # inline comment
weather_location = "old town"

[terminal]
copy_on_select = true

# Module-owned tables must survive core merges untouched.
[mega_city_code]
sign_text_px_range = [1.5, 8.0]

[mega_city_code.defaults]
sign_text_px_range = [1.5, 8.0]

[satview]
ground_station = "york"
)";

} // namespace

TEST_CASE("config parity: default AppConfig serialization snapshot", "[config][parity]")
{
    compare_against_golden(AppConfig{}.serialize(), "default_config.golden.toml");
}

TEST_CASE("config parity: kitchen-sink parse/serialize snapshot", "[config][parity]")
{
    const AppConfig config = AppConfig::parse(kKitchenSinkToml);
    compare_against_golden(config.serialize(), "kitchen_sink.golden.toml");
}

TEST_CASE("config parity: kitchen-sink survives a second round-trip unchanged", "[config][parity]")
{
    const AppConfig once = AppConfig::parse(kKitchenSinkToml);
    const std::string first = once.serialize();
    const AppConfig twice = AppConfig::parse(first);
    CHECK(twice.serialize() == first);
}

TEST_CASE("config parity: core merge into user document snapshot", "[config][parity]")
{
    draxul::tests::TempDir temp("draxul-config-parity-merge");
    const std::filesystem::path path = temp.path / "config.toml";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << kUserDocumentToml;
    }

    ConfigDocument document = ConfigDocument::load_from_path(path);
    const AppConfig config = AppConfig::parse(kKitchenSinkToml);
    document.merge_core_config(config);
    document.save_to_path(path);

    const std::string saved = draxul::tests::read_file(path);
    INFO("module tables survive the merge");
    REQUIRE(saved.find("[mega_city_code]") != std::string::npos);
    REQUIRE(saved.find("[mega_city_code.defaults]") != std::string::npos);
    REQUIRE(saved.find("[satview]") != std::string::npos);
    compare_against_golden(saved, "merged_document.golden.toml");
}

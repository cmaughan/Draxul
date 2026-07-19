// Unit tests for the declarative config schema
// (kanban/pending/21 declarative-config-schema -refactor.md).
//
// These pin the schema itself: that it enumerates exactly the AppConfig fields,
// that its query surface is consistent, that the module extension hook behaves,
// and that the generated docs fragment (docs/config-keys.generated.md) is
// current. They are independent of whether the parse/serialize hot path has
// been migrated onto the schema yet.
//
// To regenerate the docs fragment after an INTENTIONAL schema change, run:
//
//   DRAXUL_REGEN_CONFIG_DOCS=1 ./build/tests/draxul-tests "[config][docs]"
//
// and review the diff like any other code change.

#include <draxul/app_config.h>
#include <draxul/config_schema.h>

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::config_schema;

namespace
{

std::vector<std::string> all_dotted_keys()
{
    std::vector<std::string> keys;
    for (const ConfigFieldDesc& field : fields())
        keys.push_back(field.dotted_key());
    return keys;
}

// Ground-truth mirror of every AppConfig data member that maps to a config key.
// Adding a member to AppConfig/TerminalConfig/MarkdownConfig/ChromeTheme means
// adding a descriptor row AND a line here (see the completeness mandate in the
// work item). Runtime-only members (AppConfig::warnings) and the compound
// [keybindings] section are intentionally absent.
const std::vector<std::string>& expected_field_keys()
{
    static const std::vector<std::string> keys = {
        // top level
        "window_width",
        "window_height",
        "font_size",
        "atlas_size",
        "scrollback_lines",
        "enable_ligatures",
        "smooth_scroll",
        "enable_toast_notifications",
        "show_pane_status",
        "chord_timeout_ms",
        "chord_indicator_fade_ms",
        "scroll_speed",
        "palette_bg_alpha",
        "focus_border_width",
        "toast_duration_s",
        "font_path",
        "bold_font_path",
        "italic_font_path",
        "bold_italic_font_path",
        "weather_location",
        "fallback_paths",
        // [markdown]
        "markdown.font_size",
        "markdown.margin_columns",
        // [chrome]
        "chrome.tab_bar_bg",
        "chrome.tab_active_fg",
        "chrome.tab_inactive_fg",
        "chrome.tab_active_bg",
        "chrome.tab_inactive_bg",
        "chrome.tab_editing_bg",
        "chrome.divider",
        "chrome.focus_border",
        "chrome.status_bar_bg",
        "chrome.status_bar_fg",
        "chrome.status_focused_accent_bg",
        "chrome.status_inactive_accent_bg",
        "chrome.status_editing_bg",
        "chrome.resource_pill_bg",
        "chrome.resource_pill_fg",
        "chrome.resource_pill_warn_bg",
        "chrome.resource_pill_hot_bg",
        "chrome.chord_pill_bg",
        "chrome.weather_pill_bg",
        "chrome.editing_outline",
        // [terminal]
        "terminal.fg",
        "terminal.bg",
        "terminal.selection_max_cells",
        "terminal.copy_on_select",
        "terminal.paste_confirm_lines",
        "terminal.url_detection",
        "terminal.enable_osc8_hyperlinks",
        "terminal.enable_shell_integration_marks",
    };
    return keys;
}

std::filesystem::path generated_docs_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "docs" / "config-keys.generated.md";
}

std::string read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool regen_docs()
{
    const char* env = std::getenv("DRAXUL_REGEN_CONFIG_DOCS");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}

} // namespace

TEST_CASE("config schema represents every AppConfig field exactly once", "[config][schema]")
{
    const std::vector<std::string> actual = all_dotted_keys();

    // Exactly once: no duplicate dotted keys.
    std::set<std::string> unique(actual.begin(), actual.end());
    CHECK(unique.size() == actual.size());

    // Every AppConfig field is represented, and nothing extra is.
    std::set<std::string> actual_set(actual.begin(), actual.end());
    std::set<std::string> expected_set(
        expected_field_keys().begin(), expected_field_keys().end());
    CHECK(actual_set == expected_set);
    CHECK(fields().size() == expected_field_keys().size());
}

TEST_CASE("config schema sections cover the four core tables", "[config][schema]")
{
    std::vector<std::string> names;
    for (const ConfigSectionDesc& section : sections())
        names.emplace_back(section.name);
    CHECK(names == std::vector<std::string>{ "markdown", "chrome", "terminal", "keybindings" });

    // keybindings is the only compound section; markdown + keybindings always emit.
    for (const ConfigSectionDesc& section : sections())
    {
        if (section.name == "keybindings")
        {
            CHECK(section.compound);
            CHECK(section.always_emit);
        }
        else
        {
            CHECK_FALSE(section.compound);
        }
        if (section.name == "chrome")
            CHECK(section.warn_unknown_keys);
    }
}

TEST_CASE("config schema top-level key inventory matches merge ownership", "[config][schema]")
{
    // Every field default must be reachable through its accessor and every
    // dotted key must be well-formed.
    for (const ConfigFieldDesc& field : fields())
    {
        CHECK_FALSE(field.key.empty());
        CHECK_FALSE(field.dotted_key().empty());
    }

    // is_core_top_level_key agrees with for_each_core_top_level_key, and the
    // count is the 21 scalar/list keys + 4 section tables the merge owns.
    std::vector<std::string> enumerated;
    for_each_core_top_level_key([&](std::string_view key) {
        enumerated.emplace_back(key);
        CHECK(is_core_top_level_key(key));
    });
    CHECK(enumerated.size() == 25);

    CHECK(is_core_top_level_key("window_width"));
    CHECK(is_core_top_level_key("keybindings"));
    CHECK(is_core_top_level_key("terminal"));
    CHECK_FALSE(is_core_top_level_key("mega_city_code"));
    CHECK_FALSE(is_core_top_level_key("fg")); // section-qualified, not top level
}

TEST_CASE("config schema module extension hook registers and dedupes", "[config][schema]")
{
    const std::size_t before = module_tables().size();

    register_module_table("draxul_test_module", "first description");
    register_module_table("draxul_test_module", "second description");

    const auto found = std::find_if(module_tables().begin(), module_tables().end(),
        [](const ModuleTableDesc& desc) { return desc.name == "draxul_test_module"; });
    REQUIRE(found != module_tables().end());
    CHECK(found->description == "second description");
    CHECK(module_tables().size() == before + 1); // idempotent per name

    // A name that collides with a core key is rejected.
    register_module_table("terminal", "should be ignored");
    CHECK(module_tables().size() == before + 1);
    const bool has_terminal = std::any_of(module_tables().begin(), module_tables().end(),
        [](const ModuleTableDesc& desc) { return desc.name == "terminal"; });
    CHECK_FALSE(has_terminal);
}

TEST_CASE("config schema generated docs fragment is current", "[config][docs]")
{
    const std::string rendered = render_config_docs_markdown();
    const std::filesystem::path path = generated_docs_path();

    if (regen_docs())
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << rendered;
    }

    INFO("generated docs fragment: " << path.string());
    INFO("regenerate with DRAXUL_REGEN_CONFIG_DOCS=1 if this schema change is intentional");
    const std::string on_disk = read_file_bytes(path);
    REQUIRE_FALSE(on_disk.empty());
    CHECK(rendered == on_disk);

    // Spot-check the rendering actually contains schema content.
    CHECK(rendered.find("window_width") != std::string::npos);
    CHECK(rendered.find("[chrome]") != std::string::npos);
    CHECK(rendered.find("[keybindings]") != std::string::npos);
}

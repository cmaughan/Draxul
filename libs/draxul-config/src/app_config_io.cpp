#include "config_schema_driver.h"

#include <draxul/app_config_types.h>
#include <draxul/config_schema.h>
#include <draxul/gui_actions.h>
#include <draxul/keybinding_parser.h>
#include <draxul/perf_timing.h>
#include <draxul/toml_support.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <draxul/log.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace draxul
{

namespace
{

constexpr int kMinWindowWidth = 640;
constexpr int kMinWindowHeight = 400;
constexpr int kMaxWindowWidth = 3840;
constexpr int kMaxWindowHeight = 2160;
constexpr int kMinAtlasSize = 1024;
constexpr int kMaxAtlasSize = 8192;
constexpr int kMinScrollbackLines = 1;
constexpr int kMaxScrollbackLines = 1000000;
// Mirror TextService::MIN/MAX_POINT_SIZE so that draxul-config does not need to
// link draxul-font. Keep in sync with text_service.h if those values change.
constexpr float kMinFontPointSize = 6.0f;
constexpr float kMaxFontPointSize = 72.0f;
constexpr float kMinMarkdownMarginColumns = 0.0f;
constexpr float kMaxMarkdownMarginColumns = 24.0f;
// kGuiModifierMask is defined in input_types.h as kGuiModifierMask (same bit values).
// The list of known GUI action keys lives in <draxul/gui_actions.h> as the canonical
// source of truth. Use is_known_gui_action_config_key() / for_each_gui_action_config_key().
// The known top-level key inventory (the 21 scalar/list keys plus the four
// section tables) comes from the config schema: config_schema::is_core_top_level_key.

constexpr std::array<std::string_view, 20> kKnownChromeKeys = {
    "tab_bar_bg",
    "tab_active_fg",
    "tab_inactive_fg",
    "tab_active_bg",
    "tab_inactive_bg",
    "tab_editing_bg",
    "divider",
    "focus_border",
    "status_bar_bg",
    "status_bar_fg",
    "status_focused_accent_bg",
    "status_inactive_accent_bg",
    "status_editing_bg",
    "resource_pill_bg",
    "resource_pill_fg",
    "resource_pill_warn_bg",
    "resource_pill_hot_bg",
    "chord_pill_bg",
    "weather_pill_bg",
    "editing_outline",
};

std::filesystem::path config_path()
{
    PERF_MEASURE();
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || appdata[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "APPDATA is not set or empty; using fallback config path");
        appdata = nullptr;
    }
    std::filesystem::path base = appdata ? appdata : ".";
    return base / "draxul" / "config.toml";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "HOME is not set or empty; using fallback config path");
        home = nullptr;
    }
    std::filesystem::path base = home ? home : ".";
    return base / "Library" / "Application Support" / "draxul" / "config.toml";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "XDG_CONFIG_HOME is empty; using fallback config path");
        xdg = nullptr;
    }
    if (home && home[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "HOME is empty; using fallback config path");
        home = nullptr;
    }
    std::filesystem::path base = xdg ? xdg : (home ? std::filesystem::path(home) / ".config" : std::filesystem::path("."));
    return base / "draxul" / "config.toml";
#endif
}

int parse_window_dimension(const toml::table& document, const char* key, int fallback, int min_value, int max_value)
{
    if (auto parsed = toml_support::get_int(document, key); parsed.has_value())
    {
        if (*parsed < min_value || *parsed > max_value)
            return fallback;
        return static_cast<int>(*parsed);
    }
    return fallback;
}

int floor_to_power_of_two(int value)
{
    PERF_MEASURE();
    if (value <= 0)
        return 0;
    int result = 1;
    while (result * 2 <= value)
        result *= 2;
    return result;
}

int parse_atlas_size(const toml::table& document, int fallback)
{
    PERF_MEASURE();
    if (auto parsed = toml_support::get_int(document, "atlas_size"); parsed.has_value())
    {
        auto clamped = static_cast<int>(std::clamp(*parsed, static_cast<int64_t>(kMinAtlasSize), static_cast<int64_t>(kMaxAtlasSize)));
        return floor_to_power_of_two(clamped);
    }
    return fallback;
}

int parse_scrollback_lines(const toml::table& document, int fallback)
{
    if (auto parsed = toml_support::get_int(document, "scrollback_lines"); parsed.has_value())
    {
        if (*parsed < kMinScrollbackLines || *parsed > kMaxScrollbackLines)
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "[config] scrollback_lines %lld out of range [%d,%d] -- using default",
                static_cast<long long>(*parsed), kMinScrollbackLines, kMaxScrollbackLines);
            return fallback;
        }
        return static_cast<int>(*parsed);
    }
    return fallback;
}

void replace_gui_keybinding(std::vector<GuiKeybinding>& bindings, GuiKeybinding binding)
{
    PERF_MEASURE();
    std::erase_if(bindings, [&binding](const GuiKeybinding& existing) { return existing.action == binding.action; });
    bindings.push_back(std::move(binding));
}

bool remove_gui_keybinding(std::vector<GuiKeybinding>& bindings, std::string_view action)
{
    PERF_MEASURE();
    const size_t original_size = bindings.size();
    std::erase_if(bindings, [action](const GuiKeybinding& existing) { return existing.action == action; });
    return bindings.size() != original_size;
}

const GuiKeybinding* first_binding_for_action(const std::vector<GuiKeybinding>& bindings, std::string_view action)
{
    PERF_MEASURE();
    auto it = std::find_if(bindings.begin(), bindings.end(),
        [&](const GuiKeybinding& binding) { return binding.action == action; });
    return it != bindings.end() ? &*it : nullptr;
}

float parse_font_size(const toml::table& document, float fallback)
{
    PERF_MEASURE();
    // Accept both integer (font_size = 14) and float (font_size = 14.5) TOML values.
    if (auto parsed = toml_support::get_double(document, "font_size"); parsed.has_value())
        return std::clamp(static_cast<float>(*parsed), kMinFontPointSize, kMaxFontPointSize);
    if (auto parsed = toml_support::get_int(document, "font_size"); parsed.has_value())
        return std::clamp(static_cast<float>(*parsed), kMinFontPointSize, kMaxFontPointSize);
    return fallback;
}

std::optional<float> parse_float_value(const toml::table& document, const char* key)
{
    if (auto parsed = toml_support::get_double(document, key); parsed.has_value())
        return static_cast<float>(*parsed);
    if (auto parsed = toml_support::get_int(document, key); parsed.has_value())
        return static_cast<float>(*parsed);
    return std::nullopt;
}

bool parse_enable_ligatures(const toml::table& document, bool fallback)
{
    return toml_support::get_bool(document, "enable_ligatures").value_or(fallback);
}

void apply_gui_keybindings(AppConfig& config, const toml::table& keybindings)
{
    PERF_MEASURE();
    for (const auto& [action_key, value] : keybindings)
    {
        if (!value.is_string())
            continue;

        auto action = std::string(action_key.str());
        auto combo = value.value<std::string_view>();
        if (!combo)
            continue;

        if (combo->empty())
        {
            if (remove_gui_keybinding(config.keybindings, action))
            {
                DRAXUL_LOG_INFO(LogCategory::App,
                    "Keybinding '%s' removed by user config.", action.c_str());
            }
            continue;
        }

        if (auto parsed = parse_gui_keybinding(action, *combo))
            replace_gui_keybinding(config.keybindings, std::move(*parsed));
    }
}

void apply_terminal_overrides(AppConfig& config, const toml::table& terminal)
{
    PERF_MEASURE();
    if (auto fg = toml_support::get_string(terminal, "fg"))
    {
        if (auto parsed = parse_hex_color(*fg); parsed.has_value())
            config.terminal.fg = *fg;
        else
            DRAXUL_LOG_WARN(LogCategory::App, "[config] terminal.fg '%s' is not a valid hex color (#RRGGBB or #RGB) -- ignoring", fg->c_str());
    }

    if (auto bg = toml_support::get_string(terminal, "bg"))
    {
        if (auto parsed = parse_hex_color(*bg); parsed.has_value())
            config.terminal.bg = *bg;
        else
            DRAXUL_LOG_WARN(LogCategory::App, "[config] terminal.bg '%s' is not a valid hex color (#RRGGBB or #RGB) -- ignoring", bg->c_str());
    }

    if (auto cells = toml_support::get_int(terminal, "selection_max_cells"))
    {
        constexpr int kMin = 256;
        constexpr int kMax = 1048576;
        if (*cells < kMin || *cells > kMax)
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "[config] terminal.selection_max_cells %lld out of range [%d,%d] -- using default",
                static_cast<long long>(*cells), kMin, kMax);
        }
        else
        {
            config.terminal.selection_max_cells = static_cast<int>(*cells);
        }
    }

    if (auto cos = toml_support::get_bool(terminal, "copy_on_select"))
        config.terminal.copy_on_select = *cos;

    if (auto pcl = toml_support::get_int(terminal, "paste_confirm_lines"))
    {
        if (*pcl < 0 || *pcl > 100000)
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "[config] terminal.paste_confirm_lines %lld out of range [0,100000] -- using default",
                static_cast<long long>(*pcl));
        }
        else
        {
            config.terminal.paste_confirm_lines = static_cast<int>(*pcl);
        }
    }
    if (auto parsed = toml_support::get_bool(terminal, "url_detection"))
        config.terminal.url_detection = *parsed;
    if (auto parsed = toml_support::get_bool(terminal, "enable_osc8_hyperlinks"))
        config.terminal.enable_osc8_hyperlinks = *parsed;
    if (auto parsed = toml_support::get_bool(terminal, "enable_shell_integration_marks"))
        config.terminal.enable_shell_integration_marks = *parsed;
}

void apply_chrome_overrides(AppConfig& config, const toml::table& chrome)
{
    PERF_MEASURE();
    auto apply_color = [&](const char* key, Color& target) {
        if (auto value = toml_support::get_string(chrome, key))
        {
            if (auto parsed = parse_hex_color(*value); parsed.has_value())
                target = *parsed;
            else
                DRAXUL_LOG_WARN(LogCategory::App,
                    "[config] chrome.%s '%s' is not a valid hex color (#RRGGBB or #RGB) -- ignoring",
                    key, value->c_str());
        }
    };

    apply_color("tab_bar_bg", config.chrome.tab_bar_bg);
    apply_color("tab_active_fg", config.chrome.tab_active_fg);
    apply_color("tab_inactive_fg", config.chrome.tab_inactive_fg);
    apply_color("tab_active_bg", config.chrome.tab_active_bg);
    apply_color("tab_inactive_bg", config.chrome.tab_inactive_bg);
    apply_color("tab_editing_bg", config.chrome.tab_editing_bg);
    apply_color("divider", config.chrome.divider);
    apply_color("focus_border", config.chrome.focus_border);
    apply_color("status_bar_bg", config.chrome.status_bar_bg);
    apply_color("status_bar_fg", config.chrome.status_bar_fg);
    apply_color("status_focused_accent_bg", config.chrome.status_focused_accent_bg);
    apply_color("status_inactive_accent_bg", config.chrome.status_inactive_accent_bg);
    apply_color("status_editing_bg", config.chrome.status_editing_bg);
    apply_color("resource_pill_bg", config.chrome.resource_pill_bg);
    apply_color("resource_pill_fg", config.chrome.resource_pill_fg);
    apply_color("resource_pill_warn_bg", config.chrome.resource_pill_warn_bg);
    apply_color("resource_pill_hot_bg", config.chrome.resource_pill_hot_bg);
    apply_color("chord_pill_bg", config.chrome.chord_pill_bg);
    apply_color("weather_pill_bg", config.chrome.weather_pill_bg);
    apply_color("editing_outline", config.chrome.editing_outline);

    for (const auto& [key, value] : chrome)
    {
        std::string_view key_sv = key.str();
        const bool known = std::find(kKnownChromeKeys.begin(), kKnownChromeKeys.end(), key_sv) != kKnownChromeKeys.end();
        if (!known)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "[config] Unknown chrome key 'chrome.%.*s' -- check spelling",
                static_cast<int>(key_sv.size()), key_sv.data());
            std::string warning = "Unknown chrome config key: ";
            warning.append(key_sv);
            config.warnings.push_back(std::move(warning));
        }
        (void)value;
    }
}

void apply_markdown_overrides(AppConfig& config, const toml::table& markdown)
{
    PERF_MEASURE();
    if (auto parsed = parse_float_value(markdown, "font_size"); parsed.has_value())
        config.markdown.font_size = std::clamp(*parsed, kMinFontPointSize, kMaxFontPointSize);
    if (auto parsed = parse_float_value(markdown, "margin_columns"); parsed.has_value())
        config.markdown.margin_columns = std::clamp(
            *parsed,
            kMinMarkdownMarginColumns,
            kMaxMarkdownMarginColumns);
}

AppConfig config_from_toml(const toml::table& document, std::string* validation_error = nullptr)
{
    PERF_MEASURE();
    AppConfig config;

    const auto report_type_error = [&](std::string_view key, std::string_view expected, const toml::node& node) {
        DRAXUL_LOG_ERROR(LogCategory::App,
            "[config] Key '%.*s' has wrong type (expected %.*s) -- using default",
            static_cast<int>(key.size()), key.data(),
            static_cast<int>(expected.size()), expected.data());
        if (validation_error && validation_error->empty())
        {
            *validation_error = "Key '" + std::string(key) + "' has wrong type (expected "
                + std::string(expected) + ") at line "
                + std::to_string(static_cast<std::size_t>(node.source().begin.line));
        }
    };

    // Warn on type mismatches for integer keys
    auto check_int_type = [&](const char* key) {
        if (const toml::node* node = document.get(key); node && !node->is_integer())
            report_type_error(key, "integer", *node);
    };
    auto check_bool_type = [&](const char* key) {
        if (const toml::node* node = document.get(key); node && !node->is_boolean())
            report_type_error(key, "boolean", *node);
    };
    auto check_string_type = [&](const char* key) {
        if (const toml::node* node = document.get(key); node && !node->is_string())
            report_type_error(key, "string", *node);
    };
    auto check_array_type = [&](const char* key) {
        if (const toml::node* node = document.get(key); node && !node->is_array())
            report_type_error(key, "array", *node);
    };

    // font_size and scroll_speed accept both integer and floating-point TOML values.
    auto check_font_size_type = [&]() {
        if (const toml::node* node = document.get("font_size");
            node && !node->is_integer() && !node->is_floating_point())
            report_type_error("font_size", "integer or float", *node);
    };
    auto check_float_type = [&](const char* key) {
        if (const toml::node* node = document.get(key);
            node && !node->is_integer() && !node->is_floating_point())
            report_type_error(key, "integer or float", *node);
    };
    auto check_nested_float_type = [&](const toml::table* table, const char* table_name, const char* key) {
        if (table == nullptr)
            return;
        if (const toml::node* node = table->get(key);
            node && !node->is_integer() && !node->is_floating_point())
            report_type_error(std::string(table_name) + "." + key, "integer or float", *node);
    };

    check_int_type("window_width");
    check_int_type("window_height");
    check_font_size_type();
    check_int_type("atlas_size");
    check_int_type("scrollback_lines");
    check_bool_type("enable_ligatures");
    check_bool_type("smooth_scroll");
    check_bool_type("enable_toast_notifications");
    check_bool_type("show_pane_status");
    check_int_type("chord_timeout_ms");
    check_int_type("chord_indicator_fade_ms");
    check_float_type("scroll_speed");
    check_float_type("palette_bg_alpha");
    check_float_type("focus_border_width");
    check_float_type("toast_duration_s");
    check_string_type("font_path");
    check_string_type("bold_font_path");
    check_string_type("italic_font_path");
    check_string_type("bold_italic_font_path");
    check_string_type("weather_location");
    check_array_type("fallback_paths");
    const toml::table* markdown_table = document["markdown"].as_table();
    if (const toml::node* node = document.get("markdown"); node && markdown_table == nullptr)
        report_type_error("markdown", "table", *node);
    const toml::table* chrome_table = document["chrome"].as_table();
    if (const toml::node* node = document.get("chrome"); node && chrome_table == nullptr)
        report_type_error("chrome", "table", *node);
    if (const toml::node* node = document.get("terminal"); node && !node->is_table())
        report_type_error("terminal", "table", *node);
    if (const toml::node* node = document.get("keybindings"); node && !node->is_table())
        report_type_error("keybindings", "table", *node);
    check_nested_float_type(markdown_table, "markdown", "font_size");
    check_nested_float_type(markdown_table, "markdown", "margin_columns");

    if (const toml::array* fallbacks = document["fallback_paths"].as_array())
    {
        for (const toml::node& entry : *fallbacks)
        {
            if (!entry.is_string())
            {
                report_type_error("fallback_paths[]", "string", entry);
                break;
            }
        }
    }
    if (const toml::table* terminal_table = document["terminal"].as_table())
    {
        const auto check_terminal = [&](const char* key, auto predicate, std::string_view expected) {
            if (const toml::node* node = terminal_table->get(key); node && !predicate(*node))
                report_type_error(std::string("terminal.") + key, expected, *node);
        };
        check_terminal("fg", [](const toml::node& node) { return node.is_string(); }, "string");
        check_terminal("bg", [](const toml::node& node) { return node.is_string(); }, "string");
        check_terminal("selection_max_cells", [](const toml::node& node) { return node.is_integer(); }, "integer");
        check_terminal("copy_on_select", [](const toml::node& node) { return node.is_boolean(); }, "boolean");
        check_terminal("paste_confirm_lines", [](const toml::node& node) { return node.is_integer(); }, "integer");
        check_terminal("url_detection", [](const toml::node& node) { return node.is_boolean(); }, "boolean");
        check_terminal("enable_osc8_hyperlinks", [](const toml::node& node) { return node.is_boolean(); }, "boolean");
        check_terminal("enable_shell_integration_marks", [](const toml::node& node) { return node.is_boolean(); }, "boolean");
    }
    if (chrome_table)
    {
        for (std::string_view key : kKnownChromeKeys)
        {
            if (const toml::node* node = chrome_table->get(key); node && !node->is_string())
                report_type_error(std::string("chrome.") + std::string(key), "string", *node);
        }
    }
    if (const toml::table* keybindings = document["keybindings"].as_table())
    {
        for (const auto& [key, value] : *keybindings)
        {
            if (!value.is_string())
                report_type_error(std::string("keybindings.") + std::string(key.str()), "string", value);
        }
    }

    config.window_width = parse_window_dimension(document, "window_width", config.window_width, kMinWindowWidth, kMaxWindowWidth);
    config.window_height = parse_window_dimension(document, "window_height", config.window_height, kMinWindowHeight, kMaxWindowHeight);
    config.font_size = parse_font_size(document, config.font_size);
    config.markdown.font_size = config.font_size;
    config.atlas_size = parse_atlas_size(document, config.atlas_size);
    config.scrollback_lines = parse_scrollback_lines(document, config.scrollback_lines);
    config.enable_ligatures = parse_enable_ligatures(document, config.enable_ligatures);
    if (auto parsed = toml_support::get_bool(document, "smooth_scroll"); parsed.has_value())
        config.smooth_scroll = *parsed;

    {
        constexpr float kScrollSpeedMin = 0.1f;
        constexpr float kScrollSpeedMax = 10.0f;
        float raw_speed = config.scroll_speed;
        if (auto parsed = toml_support::get_double(document, "scroll_speed"); parsed.has_value())
            raw_speed = static_cast<float>(*parsed);
        else if (auto parsed_int = toml_support::get_int(document, "scroll_speed"); parsed_int.has_value())
            raw_speed = static_cast<float>(*parsed_int);
        if (document["scroll_speed"])
        {
            if (raw_speed < kScrollSpeedMin || raw_speed > kScrollSpeedMax)
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "[config] scroll_speed %.2f out of range (%.1f, %.1f] -- using default 1.0",
                    static_cast<double>(raw_speed), static_cast<double>(kScrollSpeedMin), static_cast<double>(kScrollSpeedMax));
                config.scroll_speed = 1.0f;
            }
            else
            {
                config.scroll_speed = raw_speed;
            }
        }
    }

    {
        if (auto parsed = toml_support::get_double(document, "palette_bg_alpha"); parsed.has_value())
            config.palette_bg_alpha = std::clamp(static_cast<float>(*parsed), 0.0f, 1.0f);
    }

    {
        if (auto parsed = toml_support::get_double(document, "focus_border_width"); parsed.has_value())
            config.focus_border_width = std::clamp(static_cast<float>(*parsed), 1.0f, 10.0f);
        else if (auto parsed_int = toml_support::get_int(document, "focus_border_width"); parsed_int.has_value())
            config.focus_border_width = std::clamp(static_cast<float>(*parsed_int), 1.0f, 10.0f);
    }

    if (auto parsed = toml_support::get_bool(document, "enable_toast_notifications"); parsed.has_value())
        config.enable_toast_notifications = *parsed;

    if (auto parsed = toml_support::get_bool(document, "show_pane_status"); parsed.has_value())
        config.show_pane_status = *parsed;

    if (auto parsed = toml_support::get_int(document, "chord_timeout_ms"); parsed.has_value())
        config.chord_timeout_ms = std::max(100, static_cast<int>(*parsed));
    if (auto parsed = toml_support::get_int(document, "chord_indicator_fade_ms"); parsed.has_value())
        config.chord_indicator_fade_ms = std::max(100, static_cast<int>(*parsed));

    {
        constexpr float kMinToastDuration = 0.5f;
        constexpr float kMaxToastDuration = 60.0f;
        if (auto parsed = toml_support::get_double(document, "toast_duration_s"); parsed.has_value())
            config.toast_duration_s = std::clamp(static_cast<float>(*parsed), kMinToastDuration, kMaxToastDuration);
        else if (auto parsed_int = toml_support::get_int(document, "toast_duration_s"); parsed_int.has_value())
            config.toast_duration_s = std::clamp(static_cast<float>(*parsed_int), kMinToastDuration, kMaxToastDuration);
    }

    if (auto loc = toml_support::get_string(document, "weather_location"))
        config.weather_location = *loc;

    if (auto font_path = toml_support::get_string(document, "font_path"))
        config.font_path = *font_path;
    if (auto bold_font_path = toml_support::get_string(document, "bold_font_path"))
        config.bold_font_path = *bold_font_path;
    if (auto italic_font_path = toml_support::get_string(document, "italic_font_path"))
        config.italic_font_path = *italic_font_path;
    if (auto bold_italic_font_path = toml_support::get_string(document, "bold_italic_font_path"))
        config.bold_italic_font_path = *bold_italic_font_path;
    if (auto fallback_paths = toml_support::get_string_array(document, "fallback_paths"))
        config.fallback_paths = std::move(*fallback_paths);

    if (const auto* keybindings = document["keybindings"].as_table())
        apply_gui_keybindings(config, *keybindings);

    // Warn about duplicate key+modifier combinations in keybindings
    for (size_t i = 0; i < config.keybindings.size(); ++i)
    {
        for (size_t j = i + 1; j < config.keybindings.size(); ++j)
        {
            const auto& a = config.keybindings[i];
            const auto& b = config.keybindings[j];
            if (a.prefix_key == b.prefix_key && a.prefix_modifiers == b.prefix_modifiers
                && a.key == b.key && a.modifiers == b.modifiers)
                DRAXUL_LOG_WARN(LogCategory::App,
                    "[config] Duplicate keybinding: same key+modifier used for '%s' and '%s'; "
                    "'%s' takes precedence (first registered wins)",
                    a.action.c_str(), b.action.c_str(), a.action.c_str());
        }
    }

    // [terminal] section -- optional fg/bg hex color overrides for shell panes.
    if (const auto* terminal = document["terminal"].as_table())
        apply_terminal_overrides(config, *terminal);
    if (chrome_table != nullptr)
        apply_chrome_overrides(config, *chrome_table);
    if (markdown_table != nullptr)
        apply_markdown_overrides(config, *markdown_table);

    // Warn about unknown top-level keys. The ownership inventory is the schema's;
    // unknown top-level *tables* are treated as module-owned and never warned on.
    for (const auto& [key, value] : document)
    {
        std::string_view key_sv = key.str();
        bool known = config_schema::is_core_top_level_key(key_sv);
        if (!known && value.is_table())
            continue;
        if (!known)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "[config] Unknown key '%.*s' -- check spelling", static_cast<int>(key_sv.size()), key_sv.data());
            std::string warning = "Unknown config key: ";
            warning.append(key_sv);
            config.warnings.push_back(std::move(warning));
        }
    }

    return config;
}

} // namespace

// Parse a hex color string (#RRGGBB or #RGB) into a Color with alpha 1.0.
// Returns std::nullopt on malformed input.
std::optional<Color> parse_hex_color(std::string_view hex)
{
    PERF_MEASURE();
    if (hex.empty() || hex[0] != '#')
        return std::nullopt;

    hex.remove_prefix(1); // drop '#'

    auto hex_digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F')
            return 10 + (ch - 'A');
        return -1;
    };

    if (hex.size() == 6)
    {
        std::array<int, 6> digits{};
        for (int i = 0; i < 6; ++i)
        {
            digits[static_cast<size_t>(i)] = hex_digit(hex[static_cast<size_t>(i)]);
            if (digits[i] < 0)
                return std::nullopt;
        }
        const auto rgb = static_cast<uint32_t>((digits[0] << 20) | (digits[1] << 16)
            | (digits[2] << 12) | (digits[3] << 8)
            | (digits[4] << 4) | digits[5]);
        return color_from_rgb(rgb);
    }

    if (hex.size() == 3)
    {
        std::array<int, 3> digits{};
        for (int i = 0; i < 3; ++i)
        {
            digits[static_cast<size_t>(i)] = hex_digit(hex[static_cast<size_t>(i)]);
            if (digits[i] < 0)
                return std::nullopt;
        }
        // Expand #RGB to #RRGGBB: each digit is doubled (e.g. #abc -> #aabbcc)
        const auto rgb = static_cast<uint32_t>(
            ((digits[0] * 17) << 16) | ((digits[1] * 17) << 8) | (digits[2] * 17));
        return color_from_rgb(rgb);
    }

    return std::nullopt;
}

AppConfig::AppConfig()
{
    keybindings = {
        // Single-key bindings (prefix_key=0, prefix_modifiers=kModNone).
        { "toggle_diagnostics", 0, kModNone, static_cast<int32_t>(SDLK_F12), kModNone },
        { "toggle_host_ui", 0, kModNone, static_cast<int32_t>(SDLK_F1), kModNone },
        { "copy", 0, kModNone, static_cast<int32_t>(SDLK_C), kModCtrl | kModShift },
        { "paste", 0, kModNone, static_cast<int32_t>(SDLK_V), kModCtrl | kModShift },
        // confirm_paste = Enter (only meaningful when a paste-confirmation toast is up)
        { "confirm_paste", 0, kModNone, static_cast<int32_t>(SDLK_RETURN), kModCtrl | kModShift },
        { "cancel_paste", 0, kModNone, static_cast<int32_t>(SDLK_ESCAPE), kModCtrl | kModShift },
        // toggle_copy_mode: tmux-style copy mode (Ctrl+S, Return)
        { "toggle_copy_mode", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_RETURN), kModNone },
        { "font_increase", 0, kModNone, static_cast<int32_t>(SDLK_EQUALS), kModCtrl },
        { "font_decrease", 0, kModNone, static_cast<int32_t>(SDLK_MINUS), kModCtrl },
        { "font_reset", 0, kModNone, static_cast<int32_t>(SDLK_0), kModCtrl },
        { "command_palette", 0, kModNone, static_cast<int32_t>(SDLK_P), kModCtrl | kModShift },
        // Chord bindings: prefix key Ctrl+S (tmux-style prefix).
        // split_vertical = Ctrl+S, | (Shift+Backslash on US keyboard; SDL3 reports SDLK_BACKSLASH + kModShift)
        { "split_vertical", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_BACKSLASH), kModShift },
        // split_horizontal = Ctrl+S, -
        { "split_horizontal", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_MINUS), kModNone },
        // toggle_zoom = Ctrl+S, z (tmux-style pane zoom)
        { "toggle_zoom", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_Z), kModNone },
        // close_pane = Ctrl+S, X
        { "close_pane", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_X), kModNone },
        // restart_host = Ctrl+S, R
        { "restart_host", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_R), kModNone },
        // swap_pane = Ctrl+S, O
        { "swap_pane", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_O), kModNone },
        // Pane focus navigation: Ctrl+H/J/K/L (vim-style)
        { "focus_left", 0, kModNone, static_cast<int32_t>(SDLK_H), kModCtrl },
        { "focus_down", 0, kModNone, static_cast<int32_t>(SDLK_J), kModCtrl },
        { "focus_up", 0, kModNone, static_cast<int32_t>(SDLK_K), kModCtrl },
        { "focus_right", 0, kModNone, static_cast<int32_t>(SDLK_L), kModCtrl },
        // Pane resize: Ctrl+S, arrow (tmux-style — nudges nearest divider by 5%)
        { "resize_pane_left", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_LEFT), kModNone },
        { "resize_pane_right", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_RIGHT), kModNone },
        { "resize_pane_up", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_UP), kModNone },
        { "resize_pane_down", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_DOWN), kModNone },
        // Tab/workspace management: Ctrl+S chord prefix (tmux-style)
        // new_tab = Ctrl+S, C
        { "new_tab", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_C), kModNone },
        // close_tab = Ctrl+S, & (Shift+7)
        { "close_tab", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_7), kModShift },
        // next_tab = Ctrl+S, N
        { "next_tab", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_N), kModNone },
        // prev_tab = Ctrl+S, P
        { "prev_tab", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_P), kModNone },
        // rename_tab = Ctrl+S, ,  (mirrors tmux's `<prefix> ,` for rename-window)
        { "rename_tab", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_COMMA), kModNone },
        // rename_pane = Ctrl+S, .  (paired with rename_tab; tmux has no native pane rename)
        { "rename_pane", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_PERIOD), kModNone },
        // activate_tab:N = Ctrl+S, 1-9
        { "activate_tab:1", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_1), kModNone },
        { "activate_tab:2", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_2), kModNone },
        { "activate_tab:3", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_3), kModNone },
        { "activate_tab:4", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_4), kModNone },
        { "activate_tab:5", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_5), kModNone },
        { "activate_tab:6", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_6), kModNone },
        { "activate_tab:7", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_7), kModNone },
        { "activate_tab:8", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_8), kModNone },
        { "activate_tab:9", static_cast<int32_t>(SDLK_S), kModCtrl, static_cast<int32_t>(SDLK_9), kModNone },
    };
}

AppConfig AppConfig::parse(std::string_view content)
{
    PERF_MEASURE();
    if (auto document = toml_support::parse_document(content))
        return config_from_toml(*document);
    return {};
}

Result<AppConfig, Error> parse_app_config_checked(
    std::string_view content,
    std::string_view source_name)
{
    PERF_MEASURE();
    std::string parse_error;
    auto document = toml_support::parse_document(content, &parse_error);
    if (!document)
    {
        return Result<AppConfig, Error>::err(Error::config_parse(
            "Failed to parse config " + std::string(source_name) + ": " + parse_error));
    }
    std::string validation_error;
    AppConfig config = config_from_toml(*document, &validation_error);
    if (!validation_error.empty())
    {
        return Result<AppConfig, Error>::err(Error::config_parse(
            "Failed to validate config " + std::string(source_name) + ": " + validation_error));
    }
    return Result<AppConfig, Error>::ok(std::move(config));
}

std::string AppConfig::serialize() const
{
    PERF_MEASURE();
    // Top-level scalars plus the [markdown], [chrome], and [terminal] tables are
    // emitted by the schema driver (config_schema.cpp) from the descriptor table:
    // per-field serialize range rules (clamp / power-of-two / default) and emit
    // policy (always / skip-if-empty / skip-if-default) live there now.
    toml::table document;
    config_schema::serialize_fields(*this, document);

    // The compound [keybindings] table keeps its dedicated writer.
    toml::table keybinding_table;
    for_each_gui_action_config_key([&](std::string_view action) {
        if (const GuiKeybinding* binding = first_binding_for_action(keybindings, action))
        {
            std::string combo;
            if (binding->prefix_key != 0)
                combo = format_gui_keybinding_combo(binding->prefix_key, binding->prefix_modifiers) + ", "
                    + format_gui_keybinding_combo(binding->key, binding->modifiers);
            else
                combo = format_gui_keybinding_combo(binding->key, binding->modifiers);
            keybinding_table.insert_or_assign(std::string(action), std::move(combo));
        }
    });
    document.insert_or_assign("keybindings", std::move(keybinding_table));

    std::ostringstream out;
    out << document << '\n';
    return out.str();
}

AppConfig AppConfig::load()
{
    return load_from_path(config_path());
}

void AppConfig::save() const
{
    save_to_path(config_path());
}

AppConfig AppConfig::load_from_path(const std::filesystem::path& path)
{
    PERF_MEASURE();
    try
    {
        if (!std::filesystem::exists(path))
            return {};
        std::string parse_error;
        auto document = toml_support::parse_file(path, &parse_error);
        if (!document)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to parse config from %s: %s",
                path.string().c_str(), parse_error.c_str());
            return {};
        }
        return config_from_toml(*document);
    }
    catch (const std::exception& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to load config from %s: %s",
            path.string().c_str(), ex.what());
        return {};
    }
}

Result<AppConfig, Error> load_app_config_from_path_checked(const std::filesystem::path& path)
{
    PERF_MEASURE();
    try
    {
        if (!std::filesystem::exists(path))
            return Result<AppConfig, Error>::ok(AppConfig{});

        std::string parse_error;
        auto document = toml_support::parse_file(path, &parse_error);
        if (!document)
        {
            if (parse_error == "Unable to open TOML file")
                return Result<AppConfig, Error>::err(Error::config_load(
                    "Failed to open config for reading: " + path.string()));
            return Result<AppConfig, Error>::err(Error::config_parse(
                "Failed to parse config from " + path.string() + ": " + parse_error));
        }

        std::string validation_error;
        AppConfig config = config_from_toml(*document, &validation_error);
        if (!validation_error.empty())
        {
            return Result<AppConfig, Error>::err(Error::config_parse(
                "Failed to validate config " + path.string() + ": " + validation_error));
        }
        return Result<AppConfig, Error>::ok(std::move(config));
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        return Result<AppConfig, Error>::err(Error::config_load(
            "Failed to load config from " + path.string() + ": " + ex.what()));
    }
    catch (const std::ios_base::failure& ex)
    {
        return Result<AppConfig, Error>::err(Error::config_load(
            "Failed to load config from " + path.string() + ": " + ex.what()));
    }
}

void AppConfig::save_to_path(const std::filesystem::path& path) const
{
    PERF_MEASURE();
    try
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to open config for writing: %s", path.string().c_str());
            return;
        }

        out << serialize();
        if (!out)
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to write config to %s", path.string().c_str());
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to save config to %s: %s", path.string().c_str(), ex.what());
    }
    catch (const std::ios_base::failure& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to save config to %s: %s", path.string().c_str(), ex.what());
    }
}

void apply_overrides(AppConfig& config, const AppConfigOverrides& overrides)
{
    PERF_MEASURE();
    // Helper: copy the override value into the destination if the optional holds a value.
    auto apply = [](auto& dest, const auto& src) {
        if (src)
            dest = *src;
    };
    apply(config.window_width, overrides.window_width);
    apply(config.window_height, overrides.window_height);
    apply(config.font_size, overrides.font_size);
    apply(config.markdown.font_size, overrides.markdown_font_size);
    apply(config.markdown.margin_columns, overrides.markdown_margin_columns);
    apply(config.atlas_size, overrides.atlas_size);
    apply(config.enable_ligatures, overrides.enable_ligatures);
    apply(config.font_path, overrides.font_path);
    apply(config.bold_font_path, overrides.bold_font_path);
    apply(config.italic_font_path, overrides.italic_font_path);
    apply(config.bold_italic_font_path, overrides.bold_italic_font_path);
    apply(config.fallback_paths, overrides.fallback_paths);
}

} // namespace draxul

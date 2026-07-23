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

// kGuiModifierMask is defined in input_types.h as kGuiModifierMask (same bit values).
// The list of known GUI action keys lives in <draxul/gui_actions.h> as the canonical
// source of truth. Use is_known_gui_action_config_key() / for_each_gui_action_config_key().

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

    // Wrong-type pass: every schema field, section table, fallback element, and
    // keybinding entry is checked against its descriptor ValueKind, in the same
    // order as before, so the first-reported error in a checked parse is stable.
    config_schema::check_types(document, report_type_error);

    config_schema::parse_top_level_fields(document, config);

    // Markdown inherits the parsed global size unless its own section overrides
    // it. This is the sole cross-field rule and deliberately sits between the
    // schema driver's top-level and section passes.
    config.markdown.font_size = config.font_size;
    config_schema::parse_section_fields(document, config);

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

    config_schema::warn_unknown_keys(document, config);

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
        // quit = Ctrl+S, Q
        { "quit", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_Q), kModNone },
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
        // Tab/tab management: Ctrl+S chord prefix (tmux-style)
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

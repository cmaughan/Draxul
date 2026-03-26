#include <draxul/app_config_types.h>
#include <draxul/keybinding_parser.h>
#include <draxul/text_service.h>
#include <draxul/toml_support.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <draxul/log.h>
#include <filesystem>
#include <fstream>
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
// kGuiModifierMask is defined in input_types.h as kGuiModifierMask (same bit values).
constexpr std::array<std::string_view, 9> kKnownGuiActions = {
    "toggle_diagnostics",
    "copy",
    "paste",
    "font_increase",
    "font_decrease",
    "font_reset",
    "open_file_dialog",
    "split_vertical",
    "split_horizontal",
};
constexpr std::array<std::string_view, 14> kKnownTopLevelKeys = {
    "window_width",
    "window_height",
    "font_size",
    "atlas_size",
    "enable_ligatures",
    "smooth_scroll",
    "scroll_speed",
    "font_path",
    "bold_font_path",
    "italic_font_path",
    "bold_italic_font_path",
    "fallback_paths",
    "keybindings",
    "terminal",
};

std::filesystem::path config_path()
{
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
    return base / "draxul" / "config.toml";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    return base / "Library" / "Application Support" / "draxul" / "config.toml";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
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

int clamp_window_dimension(int value, int fallback, int min_value, int max_value)
{
    if (value < min_value || value > max_value)
        return fallback;
    return value;
}

int floor_to_power_of_two(int value)
{
    if (value <= 0)
        return 0;
    int result = 1;
    while (result * 2 <= value)
        result *= 2;
    return result;
}

int parse_atlas_size(const toml::table& document, int fallback)
{
    if (auto parsed = toml_support::get_int(document, "atlas_size"); parsed.has_value())
    {
        auto clamped = static_cast<int>(std::clamp(*parsed, static_cast<int64_t>(kMinAtlasSize), static_cast<int64_t>(kMaxAtlasSize)));
        return floor_to_power_of_two(clamped);
    }
    return fallback;
}

void replace_gui_keybinding(std::vector<GuiKeybinding>& bindings, GuiKeybinding binding)
{
    std::erase_if(bindings, [&binding](const GuiKeybinding& existing) { return existing.action == binding.action; });
    bindings.push_back(std::move(binding));
}

bool remove_gui_keybinding(std::vector<GuiKeybinding>& bindings, std::string_view action)
{
    const size_t original_size = bindings.size();
    std::erase_if(bindings, [action](const GuiKeybinding& existing) { return existing.action == action; });
    return bindings.size() != original_size;
}

const GuiKeybinding* first_binding_for_action(const std::vector<GuiKeybinding>& bindings, std::string_view action)
{
    auto it = std::find_if(bindings.begin(), bindings.end(),
        [&](const GuiKeybinding& binding) { return binding.action == action; });
    return it != bindings.end() ? &*it : nullptr;
}

float parse_font_size(const toml::table& document, float fallback)
{
    // Accept both integer (font_size = 14) and float (font_size = 14.5) TOML values.
    if (auto parsed = toml_support::get_double(document, "font_size"); parsed.has_value())
        return std::clamp(static_cast<float>(*parsed), TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    if (auto parsed = toml_support::get_int(document, "font_size"); parsed.has_value())
        return std::clamp(static_cast<float>(*parsed), TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    return fallback;
}

bool parse_enable_ligatures(const toml::table& document, bool fallback)
{
    return toml_support::get_bool(document, "enable_ligatures").value_or(fallback);
}

void apply_gui_keybindings(AppConfig& config, const toml::table& keybindings)
{
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
                DRAXUL_LOG_DEBUG(LogCategory::App,
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
}

AppConfig config_from_toml(const toml::table& document)
{
    AppConfig config;

    // Warn on type mismatches for integer keys
    auto check_int_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_integer())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected integer) -- using default", key);
    };
    auto check_bool_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_boolean())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected boolean) -- using default", key);
    };
    auto check_string_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_string())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected string) -- using default", key);
    };
    auto check_array_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_array())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected array) -- using default", key);
    };

    // font_size and scroll_speed accept both integer and floating-point TOML values.
    auto check_font_size_type = [&]() {
        auto node = document["font_size"];
        if (node && !node.is_integer() && !node.is_floating_point())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key 'font_size' has wrong type (expected integer or float) -- using default");
    };
    auto check_float_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_integer() && !node.is_floating_point())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected integer or float) -- using default", key);
    };

    check_int_type("window_width");
    check_int_type("window_height");
    check_font_size_type();
    check_int_type("atlas_size");
    check_bool_type("enable_ligatures");
    check_bool_type("smooth_scroll");
    check_float_type("scroll_speed");
    check_string_type("font_path");
    check_string_type("bold_font_path");
    check_string_type("italic_font_path");
    check_string_type("bold_italic_font_path");
    check_array_type("fallback_paths");

    config.window_width = parse_window_dimension(document, "window_width", config.window_width, kMinWindowWidth, kMaxWindowWidth);
    config.window_height = parse_window_dimension(document, "window_height", config.window_height, kMinWindowHeight, kMaxWindowHeight);
    config.font_size = parse_font_size(document, config.font_size);
    config.atlas_size = parse_atlas_size(document, config.atlas_size);
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

    // Warn about unknown top-level keys
    for (const auto& [key, value] : document)
    {
        std::string_view key_sv = key.str();
        bool known = std::find(kKnownTopLevelKeys.begin(), kKnownTopLevelKeys.end(), key_sv) != kKnownTopLevelKeys.end();
        if (!known && value.is_table())
            continue;
        if (!known)
            DRAXUL_LOG_WARN(LogCategory::App, "[config] Unknown key '%.*s' -- check spelling", static_cast<int>(key_sv.size()), key_sv.data());
    }

    return config;
}

} // namespace

// Parse a hex color string (#RRGGBB or #RGB) into a Color with alpha 1.0.
// Returns std::nullopt on malformed input.
std::optional<Color> parse_hex_color(std::string_view hex)
{
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
        { "copy", 0, kModNone, static_cast<int32_t>(SDLK_C), kModCtrl | kModShift },
        { "paste", 0, kModNone, static_cast<int32_t>(SDLK_V), kModCtrl | kModShift },
        { "font_increase", 0, kModNone, static_cast<int32_t>(SDLK_EQUALS), kModCtrl },
        { "font_decrease", 0, kModNone, static_cast<int32_t>(SDLK_MINUS), kModCtrl },
        { "font_reset", 0, kModNone, static_cast<int32_t>(SDLK_0), kModCtrl },
        // Chord bindings: prefix key Ctrl+S (tmux-style prefix).
        // split_vertical = Ctrl+S, | (Shift+Backslash on US keyboard; SDL3 reports SDLK_BACKSLASH + kModShift)
        { "split_vertical", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_BACKSLASH), kModShift },
        // split_horizontal = Ctrl+S, -
        { "split_horizontal", static_cast<int32_t>(SDLK_S), kModCtrl,
            static_cast<int32_t>(SDLK_MINUS), kModNone },
    };
}

AppConfig AppConfig::parse(std::string_view content)
{
    if (auto document = toml_support::parse_document(content))
        return config_from_toml(*document);
    return {};
}

std::string AppConfig::serialize() const
{
    toml::table document;
    document.insert_or_assign("window_width", clamp_window_dimension(window_width, AppConfig{}.window_width, kMinWindowWidth, kMaxWindowWidth));
    document.insert_or_assign("window_height", clamp_window_dimension(window_height, AppConfig{}.window_height, kMinWindowHeight, kMaxWindowHeight));
    document.insert_or_assign("font_size", static_cast<double>(std::clamp(font_size, TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE)));
    document.insert_or_assign("atlas_size", floor_to_power_of_two(std::clamp(atlas_size, kMinAtlasSize, kMaxAtlasSize)));
    document.insert_or_assign("enable_ligatures", enable_ligatures);
    document.insert_or_assign("smooth_scroll", smooth_scroll);
    document.insert_or_assign("scroll_speed", static_cast<double>(std::clamp(scroll_speed, 0.1f, 10.0f)));
    if (!font_path.empty())
        document.insert_or_assign("font_path", font_path);
    if (!bold_font_path.empty())
        document.insert_or_assign("bold_font_path", bold_font_path);
    if (!italic_font_path.empty())
        document.insert_or_assign("italic_font_path", italic_font_path);
    if (!bold_italic_font_path.empty())
        document.insert_or_assign("bold_italic_font_path", bold_italic_font_path);
    if (!fallback_paths.empty())
    {
        toml::array fallback_array;
        fallback_array.reserve(fallback_paths.size());
        for (const auto& fallback_path : fallback_paths)
            fallback_array.push_back(fallback_path);
        document.insert_or_assign("fallback_paths", std::move(fallback_array));
    }

    toml::table keybinding_table;
    for (std::string_view action : kKnownGuiActions)
    {
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
    }
    document.insert_or_assign("keybindings", std::move(keybinding_table));

    if (!terminal.fg.empty() || !terminal.bg.empty())
    {
        toml::table terminal_table;
        if (!terminal.fg.empty())
            terminal_table.insert_or_assign("fg", terminal.fg);
        if (!terminal.bg.empty())
            terminal_table.insert_or_assign("bg", terminal.bg);
        document.insert_or_assign("terminal", std::move(terminal_table));
    }

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
    try
    {
        if (!std::filesystem::exists(path))
            return {};

        std::string parse_error;
        auto document = toml_support::parse_file(path, &parse_error);
        if (!document)
        {
            if (parse_error == "Unable to open TOML file")
                DRAXUL_LOG_WARN(LogCategory::App, "Failed to open config for reading: %s", path.string().c_str());
            else
                DRAXUL_LOG_WARN(LogCategory::App, "Failed to parse config from %s: %s", path.string().c_str(), parse_error.c_str());
            return {};
        }

        return config_from_toml(*document);
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to load config from %s: %s", path.string().c_str(), ex.what());
        return {};
    }
    catch (const std::ios_base::failure& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to load config from %s: %s", path.string().c_str(), ex.what());
        return {};
    }
}

void AppConfig::save_to_path(const std::filesystem::path& path) const
{
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
    // Helper: copy the override value into the destination if the optional holds a value.
    auto apply = [](auto& dest, const auto& src) {
        if (src)
            dest = *src;
    };
    apply(config.window_width, overrides.window_width);
    apply(config.window_height, overrides.window_height);
    apply(config.font_size, overrides.font_size);
    apply(config.atlas_size, overrides.atlas_size);
    apply(config.enable_ligatures, overrides.enable_ligatures);
    apply(config.font_path, overrides.font_path);
    apply(config.bold_font_path, overrides.bold_font_path);
    apply(config.italic_font_path, overrides.italic_font_path);
    apply(config.bold_italic_font_path, overrides.bold_italic_font_path);
    apply(config.fallback_paths, overrides.fallback_paths);
}

} // namespace draxul

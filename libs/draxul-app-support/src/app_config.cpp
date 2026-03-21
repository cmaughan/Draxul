#include "toml_support.h"
#include <draxul/app_config.h>

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
constexpr std::array<std::string_view, 7> kKnownGuiActions = {
    "toggle_diagnostics",
    "copy",
    "paste",
    "font_increase",
    "font_decrease",
    "font_reset",
    "open_file_dialog",
};
constexpr std::array<std::string_view, 12> kKnownTopLevelKeys = {
    "window_width",
    "window_height",
    "font_size",
    "atlas_size",
    "enable_ligatures",
    "smooth_scroll",
    "font_path",
    "bold_font_path",
    "italic_font_path",
    "bold_italic_font_path",
    "fallback_paths",
    "keybindings",
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

ModifierFlags normalize_gui_modifiers(ModifierFlags mod)
{
    return mod & kGuiModifierMask;
}

bool is_known_gui_action(std::string_view action)
{
    return std::find(kKnownGuiActions.begin(), kKnownGuiActions.end(), action) != kKnownGuiActions.end();
}

bool equals_ignore_case(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

std::vector<std::string_view> split_key_combo(std::string_view combo)
{
    std::vector<std::string_view> trimmed_parts;
    size_t part_start = 0;
    while (part_start <= combo.size())
    {
        size_t plus = combo.find('+', part_start);
        std::string_view raw_part = plus == std::string_view::npos
            ? combo.substr(part_start)
            : combo.substr(part_start, plus - part_start);
        size_t begin = 0;
        size_t end = raw_part.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(raw_part[begin])))
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(raw_part[end - 1])))
            --end;
        if (begin == end)
            return {};
        trimmed_parts.push_back(raw_part.substr(begin, end - begin));
        if (plus == std::string_view::npos)
            break;
        part_start = plus + 1;
    }

    return trimmed_parts;
}

std::optional<ModifierFlags> parse_modifier_token(std::string_view token)
{
    if (equals_ignore_case(token, "ctrl") || equals_ignore_case(token, "control"))
        return kModCtrl;
    if (equals_ignore_case(token, "shift"))
        return kModShift;
    if (equals_ignore_case(token, "alt"))
        return kModAlt;
    if (equals_ignore_case(token, "super") || equals_ignore_case(token, "gui") || equals_ignore_case(token, "meta"))
        return kModSuper;
    return std::nullopt;
}

std::optional<int32_t> parse_key_token(std::string_view token)
{
    if (token.empty())
        return std::nullopt;

    if (token == "=" || equals_ignore_case(token, "equals"))
        return static_cast<int32_t>(SDLK_EQUALS);
    if (token == "-" || equals_ignore_case(token, "minus"))
        return static_cast<int32_t>(SDLK_MINUS);
    if (token == "+" || equals_ignore_case(token, "plus"))
        return static_cast<int32_t>(SDLK_PLUS);

    std::string name(token);
    if (SDL_Keycode key = SDL_GetKeyFromName(name.c_str()); key != SDLK_UNKNOWN)
        return static_cast<int32_t>(key);

    for (char& ch : name)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (SDL_Keycode key = SDL_GetKeyFromName(name.c_str()); key != SDLK_UNKNOWN)
        return static_cast<int32_t>(key);

    return std::nullopt;
}

std::string format_key_token(int32_t key)
{
    switch (key)
    {
    case SDLK_EQUALS:
        return "=";
    case SDLK_MINUS:
        return "-";
    default:
        break;
    }

    const char* key_name = SDL_GetKeyName(static_cast<SDL_Keycode>(key));
    if (!key_name || !*key_name)
        return "Unknown";
    return key_name;
}

void replace_gui_keybinding(std::vector<GuiKeybinding>& bindings, GuiKeybinding binding)
{
    std::erase_if(bindings, [&binding](const GuiKeybinding& existing) { return existing.action == binding.action; });
    bindings.push_back(std::move(binding));
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

AppConfig config_from_toml(const toml::table& document)
{
    AppConfig config;

    // Warn on type mismatches for integer keys
    auto check_int_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_integer())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected integer) — using default", key);
    };
    auto check_bool_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_boolean())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected boolean) — using default", key);
    };
    auto check_string_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_string())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected string) — using default", key);
    };
    auto check_array_type = [&](const char* key) {
        auto node = document[key];
        if (node && !node.is_array())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key '%s' has wrong type (expected array) — using default", key);
    };

    // font_size accepts both integer and floating-point TOML values.
    auto check_font_size_type = [&]() {
        auto node = document["font_size"];
        if (node && !node.is_integer() && !node.is_floating_point())
            DRAXUL_LOG_ERROR(LogCategory::App, "[config] Key 'font_size' has wrong type (expected integer or float) — using default");
    };

    check_int_type("window_width");
    check_int_type("window_height");
    check_font_size_type();
    check_int_type("atlas_size");
    check_bool_type("enable_ligatures");
    check_bool_type("smooth_scroll");
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
    {
        for (const auto& [action_key, value] : *keybindings)
        {
            if (!value.is_string())
                continue;

            auto action = std::string(action_key.str());
            auto combo = value.value<std::string_view>();
            if (!combo)
                continue;

            if (auto parsed = parse_gui_keybinding(action, *combo))
                replace_gui_keybinding(config.keybindings, std::move(*parsed));
        }
    }

    // Warn about duplicate key+modifier combinations in keybindings
    for (size_t i = 0; i < config.keybindings.size(); ++i)
    {
        for (size_t j = i + 1; j < config.keybindings.size(); ++j)
        {
            const auto& a = config.keybindings[i];
            const auto& b = config.keybindings[j];
            if (a.key == b.key && a.modifiers == b.modifiers)
                DRAXUL_LOG_WARN(LogCategory::App,
                    "[config] Duplicate keybinding: same key+modifier used for '%s' and '%s'; "
                    "'%s' takes precedence (first registered wins)",
                    a.action.c_str(), b.action.c_str(), a.action.c_str());
        }
    }

    // Warn about unknown top-level keys
    for (const auto& [key, value] : document)
    {
        std::string_view key_sv = key.str();
        bool known = std::find(kKnownTopLevelKeys.begin(), kKnownTopLevelKeys.end(), key_sv) != kKnownTopLevelKeys.end();
        if (!known)
            DRAXUL_LOG_WARN(LogCategory::App, "[config] Unknown key '%.*s' — check spelling", static_cast<int>(key_sv.size()), key_sv.data());
    }

    return config;
}

} // namespace

AppConfig::AppConfig()
{
    keybindings = {
        { "toggle_diagnostics", static_cast<int32_t>(SDLK_F12), kModNone },
        { "copy", static_cast<int32_t>(SDLK_C), kModCtrl | kModShift },
        { "paste", static_cast<int32_t>(SDLK_V), kModCtrl | kModShift },
        { "font_increase", static_cast<int32_t>(SDLK_EQUALS), kModCtrl },
        { "font_decrease", static_cast<int32_t>(SDLK_MINUS), kModCtrl },
        { "font_reset", static_cast<int32_t>(SDLK_0), kModCtrl },
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
            keybinding_table.insert_or_assign(std::string(action), format_gui_keybinding_combo(binding->key, binding->modifiers));
    }
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
    catch (const std::exception& ex)
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
    catch (const std::exception& ex)
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

std::optional<GuiKeybinding> parse_gui_keybinding(std::string_view action, std::string_view combo)
{
    if (!is_known_gui_action(action))
        return std::nullopt;

    std::vector<std::string_view> parts = split_key_combo(combo);
    if (parts.empty())
        return std::nullopt;

    ModifierFlags modifiers = kModNone;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        auto modifier = parse_modifier_token(parts[i]);
        if (!modifier.has_value())
            return std::nullopt;
        modifiers |= *modifier;
    }

    auto key = parse_key_token(parts.back());
    if (!key.has_value())
        return std::nullopt;

    return GuiKeybinding{ std::string(action), *key, normalize_gui_modifiers(modifiers) };
}

std::string format_gui_keybinding_combo(int32_t key, ModifierFlags modifiers)
{
    std::string combo;
    ModifierFlags normalized = normalize_gui_modifiers(modifiers);
    auto append_modifier = [&](ModifierFlags flag, std::string_view name) {
        if ((normalized & flag) == 0)
            return;
        if (!combo.empty())
            combo += '+';
        combo += name;
    };

    append_modifier(kModCtrl, "Ctrl");
    append_modifier(kModShift, "Shift");
    append_modifier(kModAlt, "Alt");
    append_modifier(kModSuper, "Super");

    if (!combo.empty())
        combo += '+';
    combo += format_key_token(key);
    return combo;
}

bool gui_keybinding_matches(const GuiKeybinding& binding, const KeyEvent& event)
{
    ModifierFlags expected_modifiers = normalize_gui_modifiers(binding.modifiers);
    ModifierFlags event_modifiers = normalize_gui_modifiers(event.mod);

    if (event.keycode == binding.key && event_modifiers == expected_modifiers)
        return true;

    // Preserve the historical Ctrl+= / Ctrl+Plus zoom-in behavior with a single canonical binding.
    return binding.key == static_cast<int32_t>(SDLK_EQUALS)
        && event.keycode == static_cast<int>(SDLK_PLUS)
        && expected_modifiers == kModCtrl
        && event_modifiers == (kModCtrl | kModShift);
}

} // namespace draxul

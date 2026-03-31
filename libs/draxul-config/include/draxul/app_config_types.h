#pragma once

// Pure data types for application configuration. This header intentionally avoids
// including renderer, window, or font headers so that config consumers do not
// transitively pull in those subsystems. For runtime types that depend on those
// subsystems (e.g. AppOptions), include <draxul/app_options.h>.

#include <cstdint>
#include <draxul/input_types.h>
#include <draxul/types.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

// Default font point size. Mirrors TextService::DEFAULT_POINT_SIZE so that
// app_config_types.h does not need to include <draxul/text_service.h>.
inline constexpr float kDefaultFontPointSize = 11.0f;

struct GuiKeybinding
{
    std::string action;
    // Chord prefix key -- if non-zero this is a tmux-style chord: the user presses prefix
    // first (and releases it), then presses `key`. Zero = direct single-key binding.
    int32_t prefix_key = 0;
    ModifierFlags prefix_modifiers = kModNone;
    // The action key (or the sole key for non-chord bindings).
    int32_t key = 0; // platform-neutral keycode value (SDL_Keycode cast to int32_t); 0 = unset
    ModifierFlags modifiers = kModNone;
};

// Terminal foreground/background colors. When both are empty, terminal hosts use their
// hardcoded defaults ({0.92,0.92,0.92} / {0.08,0.09,0.10}). Values are stored as
// #RRGGBB hex strings (e.g. "#eaeaea"). #RGB shorthand is accepted on parse.
struct TerminalConfig
{
    std::string fg; // e.g. "#eaeaea"
    std::string bg; // e.g. "#141617"
};

struct AppConfig
{
    AppConfig(); // defined in app_config_io.cpp; populates default keybindings

    int window_width = 1280;
    int window_height = 800;
    float font_size = kDefaultFontPointSize;
    int atlas_size = kAtlasSize;
    bool enable_ligatures = true;
    bool smooth_scroll = true;
    float scroll_speed = 1.0f;
    std::string font_path;
    std::string bold_font_path;
    std::string italic_font_path;
    std::string bold_italic_font_path;
    std::vector<std::string> fallback_paths;
    float palette_bg_alpha = 1.0f; // command palette background opacity [0.0, 1.0]
    std::vector<GuiKeybinding> keybindings = {}; // populated by AppConfig()
    TerminalConfig terminal; // [terminal] section -- fg/bg hex colors

    // Parse config from a TOML string. Returns defaults for any missing or invalid keys.
    static AppConfig parse(std::string_view content);
    // Serialize config to a string suitable for round-tripping through parse().
    std::string serialize() const;

    static AppConfig load();
    static AppConfig load_from_path(const std::filesystem::path& path);
    void save() const;
    void save_to_path(const std::filesystem::path& path) const;
};

// Overrides for AppConfig fields that can be set at runtime (e.g., from CLI or render-test
// scenarios). Each field is optional -- only present fields are applied. Adding a new
// overridable config field requires one entry here and one call in apply_overrides().
struct AppConfigOverrides
{
    std::optional<int> window_width;
    std::optional<int> window_height;
    std::optional<float> font_size;
    std::optional<int> atlas_size;
    std::optional<bool> enable_ligatures;
    std::optional<std::string> font_path;
    std::optional<std::string> bold_font_path;
    std::optional<std::string> italic_font_path;
    std::optional<std::string> bold_italic_font_path;
    std::optional<std::vector<std::string>> fallback_paths;
};

// Apply any present overrides from `overrides` onto `config`. Only fields that hold a
// value are written; absent optionals leave the corresponding config field untouched.
void apply_overrides(AppConfig& config, const AppConfigOverrides& overrides);

// Parse a hex color string (#RRGGBB or #RGB) into a Color with alpha 1.0.
// Returns std::nullopt on malformed input.
std::optional<Color> parse_hex_color(std::string_view hex);

} // namespace draxul

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
    // Maximum number of grid cells a single selection can span before it is
    // silently truncated. Default raised from the historical 8192 to 65536 so
    // cross-function code selections do not get cut. Clamped on parse.
    int selection_max_cells = 65536;
    // When true, completing a click-drag selection in a terminal pane copies
    // the selected text to the system clipboard automatically.
    bool copy_on_select = true;
    // Minimum line count in a pasted clipboard payload before the user is
    // prompted to confirm. Set to 0 to disable the confirmation prompt.
    int paste_confirm_lines = 5;
};

struct MarkdownConfig
{
    float font_size = kDefaultFontPointSize;
    float margin_columns = 2.0f;
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
    float palette_bg_alpha = 0.9f; // command palette background opacity [0.0, 1.0]
    float focus_border_width = 3.0f; // pane focus indicator thickness in pixels
    bool enable_toast_notifications = true; // master enable for non-blocking toast popups
    float toast_duration_s = 4.0f; // how long each toast remains visible before fading out
    bool show_pane_status = true; // per-pane status bar (host kind | dims | cwd) below each pane
    // How long a chord prefix (e.g. Ctrl+S) stays "armed" while waiting for the
    // second key. After this many milliseconds the chord is silently dropped
    // and any visual indicator clears. Clamped to >= 100 ms on parse.
    int chord_timeout_ms = 1500;
    // How long the top-bar chord indicator remains visible while fading out
    // after a chord completes or times out. Clamped to >= 100 ms on parse.
    int chord_indicator_fade_ms = 2500;
    // Weather pill: set to a city name ("York, UK") or lat,lon ("53.96,-1.08")
    // to show current temperature in the top-right chrome bar. Empty = disabled.
    std::string weather_location;
    std::vector<GuiKeybinding> keybindings = {}; // populated by AppConfig()
    TerminalConfig terminal; // [terminal] section -- fg/bg hex colors
    MarkdownConfig markdown; // [markdown] section -- markdown viewer layout/font options

    // Warnings collected during parse() — e.g. unknown top-level keys. Drained by App and
    // surfaced to the user via toast notifications.
    mutable std::vector<std::string> warnings;

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
    std::optional<float> markdown_font_size;
    std::optional<float> markdown_margin_columns;
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

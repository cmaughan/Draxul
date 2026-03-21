#pragma once

#include <cstdint>
#include <draxul/events.h>
#include <draxul/host_kind.h>
#include <draxul/input_types.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/types.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct GuiKeybinding
{
    std::string action;
    int32_t key = 0; // platform keycode value (e.g. SDL_Keycode); 0 = unset
    ModifierFlags modifiers = kModNone;
};

struct AppConfig
{
    AppConfig(); // defined in app_config.cpp; populates default keybindings

    int window_width = 1280;
    int window_height = 800;
    float font_size = TextService::DEFAULT_POINT_SIZE;
    int atlas_size = kAtlasSize;
    bool enable_ligatures = true;
    bool smooth_scroll = true;
    std::string font_path;
    std::string bold_font_path;
    std::string italic_font_path;
    std::string bold_italic_font_path;
    std::vector<std::string> fallback_paths;
    std::vector<GuiKeybinding> keybindings = {}; // populated by AppConfig()

    // Parse config from a TOML string. Returns defaults for any missing or invalid keys.
    static AppConfig parse(std::string_view content);
    // Serialize config to a string suitable for round-tripping through parse().
    std::string serialize() const;

    static AppConfig load();
    static AppConfig load_from_path(const std::filesystem::path& path);
    void save() const;
    void save_to_path(const std::filesystem::path& path) const;
};

std::optional<GuiKeybinding> parse_gui_keybinding(std::string_view action, std::string_view combo);
std::string format_gui_keybinding_combo(int32_t key, ModifierFlags modifiers);
bool gui_keybinding_matches(const GuiKeybinding& binding, const KeyEvent& event);

// Overrides for AppConfig fields that can be set at runtime (e.g., from CLI or render-test
// scenarios). Each field is optional — only present fields are applied. Adding a new
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

struct AppOptions
{
    // Per-field overrides that take precedence over the loaded AppConfig.
    AppConfigOverrides config_overrides;

    // Runtime / process-lifetime flags — not persisted to disk.
    bool load_user_config = true;
    bool save_user_config = true;
    bool activate_window_on_startup = true;
    bool show_diagnostics_on_startup = false;
    bool clamp_window_to_display = true;
    bool show_render_test_window = false;
    std::optional<float> override_display_ppi;
    int render_target_pixel_width = 0;
    int render_target_pixel_height = 0;
    HostKind host_kind = HostKind::Nvim;
    std::string host_command;
    std::vector<std::string> host_args;
    std::vector<std::string> startup_commands;
    std::string host_working_dir;

    // Optional factory overrides for testing — leave null in production.
    // window_init_fn: called instead of window_.initialize(); return false to simulate failure.
    // renderer_create_fn: called instead of create_renderer(); return empty RendererBundle to fail.
    std::function<bool()> window_init_fn;
    std::function<RendererBundle(int atlas_size)> renderer_create_fn;
};

} // namespace draxul

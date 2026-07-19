#include <draxul/config_schema.h>

#include <draxul/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace draxul::config_schema
{

// ---------------------------------------------------------------------------
// Descriptor table
// ---------------------------------------------------------------------------
//
// This is the single source of truth. Every app-owned config key is one row
// below; the query functions, the generated docs, and (as the migration
// proceeds) the parse/serialize/merge driver all read from here.
//
// The accessors are captureless lambdas that decay to plain function pointers,
// so the table is ordinary static data with no per-row heap or closure state.
// A field's *default* is never written down twice: it is read back through the
// accessor from a default-constructed AppConfig (see field_default_string).

namespace
{

// Field order mirrors the historical wrong-type-check order in
// app_config_io.cpp so that, once type-checking is schema-driven, the
// "first reported error wins" behavior is preserved byte-for-byte.
constexpr std::array<ConfigFieldDesc, 51> kFields = { {
    // -- top level ---------------------------------------------------------
    { "", "window_width", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.window_width; },
        RangeRule::UseDefault, RangeRule::UseDefault, 640.0, 3840.0, false, EmitRule::Always,
        "Initial window width in pixels. Out-of-range values fall back to the default." },
    { "", "window_height", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.window_height; },
        RangeRule::UseDefault, RangeRule::UseDefault, 400.0, 2160.0, false, EmitRule::Always,
        "Initial window height in pixels. Out-of-range values fall back to the default." },
    { "", "font_size", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.font_size; },
        RangeRule::Clamp, RangeRule::Clamp, 6.0, 72.0, false, EmitRule::Always,
        "Terminal/grid font point size. Clamped into range." },
    { "", "atlas_size", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.atlas_size; },
        RangeRule::PowerOfTwo, RangeRule::PowerOfTwo, 1024.0, 8192.0, false, EmitRule::Always,
        "Glyph atlas edge length in texels. Clamped, then rounded down to a power of two." },
    { "", "scrollback_lines", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.scrollback_lines; },
        RangeRule::UseDefault, RangeRule::Clamp, 1.0, 1000000.0, true, EmitRule::Always,
        "Terminal scrollback buffer size in lines." },
    { "", "enable_ligatures", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.enable_ligatures; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::Always,
        "Combine eligible two-cell programming ligatures during shaping." },
    { "", "smooth_scroll", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.smooth_scroll; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::Always,
        "Enable trackpad momentum-style scroll accumulation." },
    { "", "enable_toast_notifications", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.enable_toast_notifications; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::Always,
        "Master enable for the corner toast overlay." },
    { "", "show_pane_status", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.show_pane_status; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::Always,
        "Show the per-pane status bar (host kind | dims | cwd) below each pane." },
    { "", "chord_timeout_ms", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.chord_timeout_ms; },
        RangeRule::ClampMin, RangeRule::ClampMin, 100.0, 0.0, false, EmitRule::Always,
        "How long a chord prefix stays armed while waiting for the second key." },
    { "", "chord_indicator_fade_ms", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.chord_indicator_fade_ms; },
        RangeRule::ClampMin, RangeRule::ClampMin, 100.0, 0.0, false, EmitRule::Always,
        "How long the top-bar chord indicator remains visible while fading out." },
    { "", "scroll_speed", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.scroll_speed; },
        RangeRule::UseDefault, RangeRule::Clamp, 0.1, 10.0, true, EmitRule::Always,
        "Multiplier applied to raw scroll deltas before smooth-scroll accumulation." },
    { "", "palette_bg_alpha", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.palette_bg_alpha; },
        RangeRule::Clamp, RangeRule::Clamp, 0.0, 1.0, false, EmitRule::Always,
        "Command palette background opacity." },
    { "", "focus_border_width", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.focus_border_width; },
        RangeRule::Clamp, RangeRule::Clamp, 1.0, 10.0, false, EmitRule::Always,
        "Pane focus indicator thickness in pixels." },
    { "", "toast_duration_s", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.toast_duration_s; },
        RangeRule::Clamp, RangeRule::Clamp, 0.5, 60.0, false, EmitRule::Always,
        "How long each toast remains visible before fading out, in seconds." },
    { "", "font_path", ValueKind::String,
        +[](const AppConfig& c) -> const std::string& { return c.font_path; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Path to the primary (regular) font file. Empty uses the bundled font." },
    { "", "bold_font_path", ValueKind::String,
        +[](const AppConfig& c) -> const std::string& { return c.bold_font_path; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Path to the bold font file. Empty synthesizes bold from the regular face." },
    { "", "italic_font_path", ValueKind::String,
        +[](const AppConfig& c) -> const std::string& { return c.italic_font_path; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Path to the italic font file. Empty synthesizes italic from the regular face." },
    { "", "bold_italic_font_path", ValueKind::String,
        +[](const AppConfig& c) -> const std::string& { return c.bold_italic_font_path; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Path to the bold-italic font file." },
    { "", "weather_location", ValueKind::String,
        +[](const AppConfig& c) -> const std::string& { return c.weather_location; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "City name or lat,lon for the weather pill. Empty disables the pill." },
    { "", "fallback_paths", ValueKind::StringList,
        +[](const AppConfig& c) -> const std::vector<std::string>& { return c.fallback_paths; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Ordered fallback font files used for glyphs the primary font lacks." },

    // -- [markdown] --------------------------------------------------------
    { "markdown", "font_size", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.markdown.font_size; },
        RangeRule::Clamp, RangeRule::Clamp, 6.0, 72.0, false, EmitRule::Always,
        "Markdown viewer font point size. Defaults to the global font_size when unset." },
    { "markdown", "margin_columns", ValueKind::Float,
        +[](const AppConfig& c) -> const float& { return c.markdown.margin_columns; },
        RangeRule::Clamp, RangeRule::Clamp, 0.0, 24.0, false, EmitRule::Always,
        "Markdown viewer horizontal margin, in text columns." },

    // -- [chrome] ----------------------------------------------------------
    { "chrome", "tab_bar_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_bar_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Tab bar background color." },
    { "chrome", "tab_active_fg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_active_fg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Active tab label color." },
    { "chrome", "tab_inactive_fg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_inactive_fg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Inactive tab label color." },
    { "chrome", "tab_active_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_active_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Active tab background color." },
    { "chrome", "tab_inactive_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_inactive_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Inactive tab background color." },
    { "chrome", "tab_editing_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.tab_editing_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Tab background while its name is being edited." },
    { "chrome", "divider", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.divider; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Pane divider color." },
    { "chrome", "focus_border", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.focus_border; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Focused pane border color." },
    { "chrome", "status_bar_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.status_bar_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Status bar background color." },
    { "chrome", "status_bar_fg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.status_bar_fg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Status bar text color." },
    { "chrome", "status_focused_accent_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.status_focused_accent_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Status bar accent background for the focused pane." },
    { "chrome", "status_inactive_accent_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.status_inactive_accent_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Status bar accent background for inactive panes." },
    { "chrome", "status_editing_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.status_editing_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Status bar background while editing." },
    { "chrome", "resource_pill_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.resource_pill_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Resource pill background color." },
    { "chrome", "resource_pill_fg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.resource_pill_fg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Resource pill text color." },
    { "chrome", "resource_pill_warn_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.resource_pill_warn_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Resource pill background at the warning threshold." },
    { "chrome", "resource_pill_hot_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.resource_pill_hot_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Resource pill background at the hot threshold." },
    { "chrome", "chord_pill_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.chord_pill_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Chord indicator pill background color." },
    { "chrome", "weather_pill_bg", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.weather_pill_bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Weather pill background color." },
    { "chrome", "editing_outline", ValueKind::ColorHex,
        +[](const AppConfig& c) -> const Color& { return c.chrome.editing_outline; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Outline color drawn around an in-place editing field." },

    // -- [terminal] --------------------------------------------------------
    { "terminal", "fg", ValueKind::HexString,
        +[](const AppConfig& c) -> const std::string& { return c.terminal.fg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Terminal foreground color (#RRGGBB). Empty uses the host default." },
    { "terminal", "bg", ValueKind::HexString,
        +[](const AppConfig& c) -> const std::string& { return c.terminal.bg; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfEmpty,
        "Terminal background color (#RRGGBB). Empty uses the host default." },
    { "terminal", "selection_max_cells", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.terminal.selection_max_cells; },
        RangeRule::UseDefault, RangeRule::None, 256.0, 1048576.0, true, EmitRule::SkipIfDefault,
        "Maximum grid cells a single selection may span before truncation." },
    { "terminal", "copy_on_select", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.terminal.copy_on_select; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Copy the selection to the clipboard when a click-drag completes." },
    { "terminal", "paste_confirm_lines", ValueKind::Int,
        +[](const AppConfig& c) -> const int& { return c.terminal.paste_confirm_lines; },
        RangeRule::UseDefault, RangeRule::None, 0.0, 100000.0, true, EmitRule::SkipIfDefault,
        "Minimum line count in a paste before the confirmation prompt appears (0 disables)." },
    { "terminal", "url_detection", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.terminal.url_detection; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Detect URLs in terminal output and make them clickable." },
    { "terminal", "enable_osc8_hyperlinks", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.terminal.enable_osc8_hyperlinks; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Honor OSC 8 hyperlink escape sequences." },
    { "terminal", "enable_shell_integration_marks", ValueKind::Bool,
        +[](const AppConfig& c) -> const bool& { return c.terminal.enable_shell_integration_marks; },
        RangeRule::None, RangeRule::None, 0.0, 0.0, false, EmitRule::SkipIfDefault,
        "Honor shell-integration prompt marks (OSC 133)." },
} };

// Section order mirrors config_schema.h: markdown, chrome, terminal,
// keybindings. keybindings is a compound section handled by dedicated hooks
// (keybinding_parser.cpp) rather than per-field descriptors.
constexpr std::array<ConfigSectionDesc, 4> kSections = { {
    { "markdown", "Markdown viewer layout and font options.", false, true, false },
    { "chrome", "UI chrome color theme.", true, false, false },
    { "terminal", "Terminal/shell pane appearance and behavior.", false, false, false },
    { "keybindings", "GUI-only keyboard shortcuts (action = combo).", false, true, true },
} };

std::string format_color_hex(const Color& color)
{
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x",
        channel(color.r), channel(color.g), channel(color.b));
    return std::string(buffer);
}

// A readable, deterministic rendering of a number for the docs table. Integral
// values print without a decimal point or scientific notation ("1000000", not
// "1e+06"); fractional values keep the default short form ("0.9", "2.5").
std::string format_number_doc(double value)
{
    if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15)
        return std::to_string(static_cast<long long>(value));
    std::ostringstream out;
    out << value;
    return out.str();
}

// The default value of a field, read back through its accessor from a
// default-constructed AppConfig and rendered for the docs table.
std::string field_default_string(const ConfigFieldDesc& field)
{
    static const AppConfig defaults;
    return std::visit(
        [&](auto accessor) -> std::string {
            const auto& value = accessor(defaults);
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>)
                return value ? "true" : "false";
            else if constexpr (std::is_same_v<T, int>)
                return std::to_string(value);
            else if constexpr (std::is_same_v<T, float>)
                return format_number_doc(value);
            else if constexpr (std::is_same_v<T, std::string>)
                return value.empty() ? "(empty)" : value;
            else if constexpr (std::is_same_v<T, Color>)
                return format_color_hex(value);
            else // std::vector<std::string>
                return value.empty() ? "(empty)" : "[...]";
        },
        field.ref);
}

std::string_view kind_words(ValueKind kind)
{
    switch (kind)
    {
    case ValueKind::Bool:
        return "boolean";
    case ValueKind::Int:
        return "integer";
    case ValueKind::Float:
        return "float";
    case ValueKind::String:
        return "string";
    case ValueKind::HexString:
    case ValueKind::ColorHex:
        return "color (#RRGGBB)";
    case ValueKind::StringList:
        return "array of strings";
    }
    return "";
}

std::string range_words(const ConfigFieldDesc& field)
{
    const auto num = [](double value) { return format_number_doc(value); };
    switch (field.parse_range)
    {
    case RangeRule::None:
        return "";
    case RangeRule::Clamp:
    case RangeRule::UseDefault:
        return num(field.min) + " .. " + num(field.max);
    case RangeRule::ClampMin:
        return ">= " + num(field.min);
    case RangeRule::PowerOfTwo:
        return num(field.min) + " .. " + num(field.max) + " (power of two)";
    }
    return "";
}

void append_field_rows(std::string& out, std::string_view section)
{
    for (const ConfigFieldDesc& field : kFields)
    {
        if (field.section != section)
            continue;
        out.append("| `").append(field.key);
        out.append("` | ").append(kind_words(field.kind));
        out.append(" | `").append(field_default_string(field));
        out.append("` | ").append(range_words(field));
        out.append(" | ").append(field.description);
        out.append(" |\n");
    }
}

} // namespace

std::span<const ConfigFieldDesc> fields()
{
    return kFields;
}

std::span<const ConfigSectionDesc> sections()
{
    return kSections;
}

bool is_core_top_level_key(std::string_view key)
{
    for (const ConfigFieldDesc& field : kFields)
    {
        if (field.section.empty() && field.key == key)
            return true;
    }
    for (const ConfigSectionDesc& section : kSections)
    {
        if (section.name == key)
            return true;
    }
    return false;
}

void for_each_core_top_level_key(const std::function<void(std::string_view)>& fn)
{
    for (const ConfigFieldDesc& field : kFields)
    {
        if (field.section.empty())
            fn(field.key);
    }
    for (const ConfigSectionDesc& section : kSections)
        fn(section.name);
}

// ---------------------------------------------------------------------------
// Module extension hook
// ---------------------------------------------------------------------------

namespace
{
std::vector<ModuleTableDesc>& module_table_registry()
{
    static std::vector<ModuleTableDesc> registry;
    return registry;
}
} // namespace

void register_module_table(std::string name, std::string description)
{
    if (is_core_top_level_key(name))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "[config] Module table '%s' collides with a core config key -- ignoring registration",
            name.c_str());
        return;
    }

    auto& registry = module_table_registry();
    for (ModuleTableDesc& existing : registry)
    {
        if (existing.name == name)
        {
            existing.description = std::move(description);
            return;
        }
    }
    registry.push_back(ModuleTableDesc{ std::move(name), std::move(description) });
}

const std::vector<ModuleTableDesc>& module_tables()
{
    return module_table_registry();
}

// ---------------------------------------------------------------------------
// Generated documentation
// ---------------------------------------------------------------------------

std::string render_config_docs_markdown()
{
    std::string out;
    out.append("# Draxul configuration keys\n\n");
    out.append(
        "<!-- Generated from the config schema (libs/draxul-config/src/config_schema.cpp).\n");
    out.append(
        "     Do not edit by hand. Regenerate with:\n");
    out.append(
        "       DRAXUL_REGEN_CONFIG_DOCS=1 ./build/tests/draxul-tests \"[config][docs]\" -->\n\n");
    out.append("User settings live in `config.toml`. Every key below is owned by the core\n");
    out.append("config layer; optional product modules own their own top-level tables and are\n");
    out.append("preserved verbatim.\n\n");

    out.append("## Top-level keys\n\n");
    out.append("| Key | Type | Default | Range | Description |\n");
    out.append("|-----|------|---------|-------|-------------|\n");
    append_field_rows(out, "");
    out.push_back('\n');

    for (const ConfigSectionDesc& section : kSections)
    {
        out.append("## [").append(section.name).append("]\n\n");
        out.append(section.description).append("\n\n");
        if (section.compound)
        {
            out.append("Compound section: `action = \"combo\"` pairs. See the GUI action\n");
            out.append("registry (`libs/draxul-config/include/draxul/gui_actions.h`) for the\n");
            out.append("full list of action names and `docs/features.md` for combo syntax.\n\n");
            continue;
        }
        out.append("| Key | Type | Default | Range | Description |\n");
        out.append("|-----|------|---------|-------|-------------|\n");
        append_field_rows(out, section.name);
        out.push_back('\n');
    }

    return out;
}

} // namespace draxul::config_schema

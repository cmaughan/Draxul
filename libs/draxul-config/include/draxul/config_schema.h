#pragma once

// Declarative configuration schema.
//
// Every config key the app owns is described exactly once by a ConfigFieldDesc
// row in config_schema.cpp. From that single row the config layer derives:
//
//   - known-key checks and unknown-key warnings
//   - the wrong-type check pass (ERROR logs + checked-parse validation errors)
//   - parse + range validation into the bound AppConfig field
//   - serialization, including per-field emit policy and clamping rules
//   - core-key ownership for ConfigDocument::merge_core_config
//   - the generated documentation table (docs/config-keys.generated.md)
//
// Compound values (the [keybindings] table) keep their dedicated parser
// (keybinding_parser.cpp) and are registered through the same schema as a
// section with custom hooks, so key inventory, docs, and merge behavior still
// come from one place.
//
// Optional product modules (MegaCity, SatView, ScoreView, ...) own their own
// top-level tables. The core library never parses those tables and always
// preserves them verbatim; modules may call register_module_table() at startup
// so the schema knows about the ownership (see "Module extension hook" below).
//
// Like the rest of the config layer, the schema registry is main-thread only.

#include <draxul/app_config_types.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace draxul::config_schema
{

// What the TOML value is and how it converts to the bound field.
enum class ValueKind : uint8_t
{
    Bool, // TOML boolean -> bool
    Int, // TOML integer -> int
    Float, // TOML float or integer -> float
    String, // TOML string -> std::string
    HexString, // TOML string; must parse as #RRGGBB/#RGB; stored as the raw string
    ColorHex, // TOML string; parsed as #RRGGBB/#RGB into a Color member
    StringList, // TOML array of strings -> std::vector<std::string>
};

// How numeric values are constrained. Fields carry one rule for parse and one
// for serialize because the legacy behavior differs per key (e.g. scrollback
// falls back to the default on out-of-range parse but clamps on serialize).
enum class RangeRule : uint8_t
{
    None, // no constraint
    Clamp, // clamp into [min, max]
    ClampMin, // clamp up to min only (max unused)
    UseDefault, // out-of-range keeps/writes the default value
    PowerOfTwo, // clamp into [min, max], then round down to a power of two
};

// When serialize() emits the key.
enum class EmitRule : uint8_t
{
    Always,
    SkipIfEmpty, // strings/lists: omitted while empty
    SkipIfDefault, // omitted while equal to the default value
};

// Read-only accessors binding a descriptor to its AppConfig storage. The parse
// driver is the single place that casts const away to write through them.
using BoolRef = const bool& (*)(const AppConfig&);
using IntRef = const int& (*)(const AppConfig&);
using FloatRef = const float& (*)(const AppConfig&);
using StringRef = const std::string& (*)(const AppConfig&);
using ColorRef = const Color& (*)(const AppConfig&);
using StringListRef = const std::vector<std::string>& (*)(const AppConfig&);

using FieldRef = std::variant<BoolRef, IntRef, FloatRef, StringRef, ColorRef, StringListRef>;

struct ConfigFieldDesc
{
    std::string_view section; // "" = top level; otherwise "terminal", "chrome", "markdown"
    std::string_view key;
    ValueKind kind = ValueKind::Bool;
    FieldRef ref;
    RangeRule parse_range = RangeRule::None;
    RangeRule serialize_range = RangeRule::None;
    double min = 0.0;
    double max = 0.0;
    bool warn_out_of_range = false; // UseDefault fields: WARN when parse rejects the value
    EmitRule emit = EmitRule::Always;
    std::string_view description; // user-facing; rendered into the generated docs

    // Full dotted key ("terminal.fg") for logs and docs.
    [[nodiscard]] std::string dotted_key() const
    {
        if (section.empty())
            return std::string(key);
        std::string out(section);
        out.push_back('.');
        out.append(key);
        return out;
    }
};

// A named [section] table. Sections with `compound == true` (keybindings) are
// parsed/serialized by custom hooks registered in config_schema.cpp instead of
// per-field descriptors.
struct ConfigSectionDesc
{
    std::string_view name;
    std::string_view description;
    bool warn_unknown_keys = false; // chrome warns; terminal/markdown historically do not
    bool always_emit = false; // markdown/keybindings always serialize; others only when non-empty
    bool compound = false;
};

// The core schema: every app-owned scalar key, in the canonical check order.
[[nodiscard]] std::span<const ConfigFieldDesc> fields();

// The core [section] tables, in canonical order (markdown, chrome, terminal, keybindings).
[[nodiscard]] std::span<const ConfigSectionDesc> sections();

// True when `key` is an app-owned top-level key (a top-level field or a section
// name). Unknown non-table keys warn on parse; unknown tables are module-owned
// and always preserved.
[[nodiscard]] bool is_core_top_level_key(std::string_view key);

// Invoke `fn(std::string_view)` for every app-owned top-level key. This is the
// ownership inventory used by ConfigDocument::merge_core_config.
void for_each_core_top_level_key(const std::function<void(std::string_view)>& fn);

// ---------------------------------------------------------------------------
// Module extension hook
// ---------------------------------------------------------------------------
//
// Optional modules own their top-level config tables (e.g. [mega_city_code],
// [satview], [scoreview]). Those tables are preserved verbatim whether or not
// they are registered; registration exists so that ownership is explicit, name
// collisions with core keys are diagnosed, and future tooling (docs, doctor
// commands) can enumerate module settings without draxul-config depending on
// any product module.
//
// Call at startup (host/module registration time, main thread):
//
//   draxul::config_schema::register_module_table(
//       "mega_city_code", "MegaCity code-analysis host settings");
//
// Registration is idempotent per table name; re-registering replaces the
// stored description. Registering a name that collides with a core key logs a
// WARN and is ignored.

struct ModuleTableDesc
{
    std::string name;
    std::string description;
};

void register_module_table(std::string name, std::string description);
[[nodiscard]] const std::vector<ModuleTableDesc>& module_tables();

// ---------------------------------------------------------------------------
// Generated documentation
// ---------------------------------------------------------------------------

// Render the config key reference (docs/config-keys.generated.md) from the
// core schema. Deterministic: module registrations do not change the output.
[[nodiscard]] std::string render_config_docs_markdown();

} // namespace draxul::config_schema

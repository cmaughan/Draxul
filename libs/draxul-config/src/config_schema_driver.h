#pragma once

// Internal schema driver: applies the declarative schema (config_schema.h) to
// a toml++ document. Used by app_config_io.cpp and implemented in
// config_schema.cpp; not part of the public API.

#include <draxul/config_schema.h>
#include <toml++/toml.hpp>

#include <functional>
#include <string_view>

namespace draxul::config_schema
{

// Report callback for wrong-type keys: full dotted key, expected type words,
// and the offending node (for the source line in checked parses).
using TypeErrorFn
    = std::function<void(std::string_view key, std::string_view expected, const toml::node&)>;

// Wrong-type checks for every schema field and section table present in
// `document`, in schema order.
void check_types(const toml::table& document, const TypeErrorFn& report);

// Parse + validate top-level schema fields into `config`. Section fields are
// parsed separately so app_config_io.cpp can apply the one cross-field rule
// (markdown.font_size follows the global font_size) between the two passes.
void parse_top_level_fields(const toml::table& document, AppConfig& config);

// Parse + validate section fields (including compound sections) into `config`.
void parse_section_fields(const toml::table& document, AppConfig& config);

// Serialize every schema field of `config` into `document` following each
// field's emit rule and serialize range rule.
void serialize_fields(const AppConfig& config, toml::table& document);

// Warn about unknown keys: top-level non-table keys not in the schema, plus
// unknown keys inside sections that opt into warnings (chrome). Appends
// user-facing warnings to config.warnings.
void warn_unknown_keys(const toml::table& document, AppConfig& config);

} // namespace draxul::config_schema

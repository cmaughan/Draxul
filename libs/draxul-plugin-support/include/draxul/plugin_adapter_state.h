#pragma once

#include <draxul/plugin_host_services.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

// Typed per-pane saved-state load/save over HostServices. Bundled-build-only
// companion to plugin_adapter.h: this half links draxul-plugin-runtime-support
// and therefore does not ship with the SDK install component.

namespace draxul::plugin_support
{

inline constexpr std::string_view kPaneStateKey = "state";

struct PaneStateLoad
{
    // Parsed saved state; nullopt when nothing usable is stored.
    std::optional<nlohmann::json> state;
    // User-facing warning when a stored state exists but could not be used;
    // empty otherwise.
    std::string warning;
};

// Loads the saved per-pane JSON state under `key`. Missing storage or an
// unset key is not an error; corrupt or unreadable state yields a warning.
[[nodiscard]] PaneStateLoad load_pane_state(const HostServices& services,
    std::string_view key = kPaneStateKey);

// Persists per-pane JSON state under `key`. Returns the user-facing warning,
// empty on success.
[[nodiscard]] std::string save_pane_state(const HostServices& services,
    const nlohmann::json& state, std::string_view key = kPaneStateKey);

} // namespace draxul::plugin_support

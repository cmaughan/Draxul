#include <draxul/plugin_adapter_state.h>

namespace draxul::plugin_support
{

PaneStateLoad load_pane_state(const HostServices& services,
    std::string_view key)
{
    PaneStateLoad result;
    const StorageReadResult read
        = services.read_json(DRAXUL_PLUGIN_STORAGE_PANE, key);
    if (read.result == DRAXUL_PLUGIN_STORAGE_NOT_FOUND
        || read.result == DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE)
        return result;
    if (!read.ok())
    {
        result.warning = read.result == DRAXUL_PLUGIN_STORAGE_INVALID_JSON
            ? "saved state is corrupt"
            : "saved state could not be read";
        return result;
    }
    try
    {
        result.state = nlohmann::json::parse(read.json);
    }
    catch (...)
    {
        result.warning = "saved state is corrupt";
    }
    return result;
}

std::string save_pane_state(const HostServices& services,
    const nlohmann::json& state, std::string_view key)
{
    const uint32_t written
        = services.write_json(DRAXUL_PLUGIN_STORAGE_PANE, key, state.dump());
    return written == DRAXUL_PLUGIN_STORAGE_OK
        ? std::string{}
        : "saved state could not be written";
}

} // namespace draxul::plugin_support

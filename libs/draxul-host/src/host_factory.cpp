#include "nvim_host.h"

#include <draxul/host.h>
#include <draxul/host_registry.h>
#include <draxul/perf_timing.h>

namespace draxul
{

void register_builtin_host_providers(HostProviderRegistry& registry)
{
    PERF_MEASURE();
    registry.register_provider(HostKind::Nvim, [] {
        return std::unique_ptr<IHost>(std::make_unique<NvimHost>());
    });
}

void register_server_shell_host_metadata(
    HostProviderRegistry& registry)
{
    PERF_MEASURE();
    registry.register_metadata(HostKind::Bash);
    registry.register_metadata(HostKind::Zsh);
#ifdef _WIN32
    registry.register_metadata(HostKind::PowerShell);
    registry.register_metadata(HostKind::Wsl);
#endif
}

// Compatibility wrapper for callers that already had a HostKind in hand and
// just want a host. Newer callers should go through HostProviderRegistry.
std::unique_ptr<IHost> create_host(HostKind kind)
{
    return HostProviderRegistry::global().create(kind);
}

} // namespace draxul

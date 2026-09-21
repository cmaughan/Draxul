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

} // namespace draxul

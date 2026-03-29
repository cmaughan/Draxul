#include "nvim_host.h"

#include <draxul/host.h>
#include <draxul/perf_timing.h>

namespace draxul
{
std::unique_ptr<IHost> create_shell_host();
#ifdef _WIN32
std::unique_ptr<IHost> create_powershell_host();
std::unique_ptr<IHost> create_wsl_host();
#endif
} // namespace draxul

namespace draxul
{

std::unique_ptr<IHost> create_host(HostKind kind)
{
    PERF_MEASURE();
    switch (kind)
    {
    case HostKind::Nvim:
        return std::make_unique<NvimHost>();
    case HostKind::PowerShell:
#ifdef _WIN32
        return create_powershell_host();
#else
        return nullptr;
#endif
    case HostKind::Bash:
    case HostKind::Zsh:
        return create_shell_host();
    case HostKind::Wsl:
#ifdef _WIN32
        return create_wsl_host();
#else
        return nullptr;
#endif
    case HostKind::MegaCity:
        return nullptr; // Created via create_megacity_host() in host_manager.cpp
    }
    return nullptr;
}

} // namespace draxul

#include <draxul/host.h>
#include <draxul/host_registry.h>

int main()
{
    draxul::HostProviderRegistry registry;
    draxul::register_server_shell_host_metadata(registry);
    return registry.has(draxul::HostKind::Bash) ? 0 : 1;
}

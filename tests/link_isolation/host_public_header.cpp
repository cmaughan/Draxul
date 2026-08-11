#include <draxul/host.h>
#include <draxul/remote_terminal_host.h>

int main()
{
    auto host = draxul::create_host(draxul::HostKind::Nvim);
    draxul::RemoteTerminalHostOptions options{
        .client_id = "link-isolation",
    };
    auto remote = std::make_unique<draxul::RemoteTerminalHost>(options);
    return host || remote ? 0 : 0;
}

#include <draxul/server_client.h>
#include <draxul/remote_terminal_client.h>

int main()
{
    draxul::RemoteTerminalClientOptions options{
        .client_id = "link-isolation",
    };
    return draxul::make_server_client_id().empty()
            || options.client_id.empty()
        ? 1
        : 0;
}

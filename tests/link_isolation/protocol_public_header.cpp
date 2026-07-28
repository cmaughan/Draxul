#include <draxul/server_protocol.h>
#include <draxul/remote_terminal_protocol.h>

int main()
{
    draxul::ServerHello hello;
    hello.client_id = "link-isolation";
    draxul::RemoteTerminalVersion version{
        .server_epoch = "epoch",
        .terminal_id = "terminal",
        .generation = 1,
    };
    return hello.protocol_major == draxul::kServerProtocolMajor
            && version.generation == 1
        ? 0
        : 1;
}

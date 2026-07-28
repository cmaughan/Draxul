#include <draxul/server_protocol.h>

int main()
{
    draxul::ServerHello hello;
    hello.client_id = "link-isolation";
    return hello.protocol_major == draxul::kServerProtocolMajor ? 0 : 1;
}

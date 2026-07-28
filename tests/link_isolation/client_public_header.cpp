#include <draxul/server_client.h>

int main()
{
    return draxul::make_server_client_id().empty() ? 1 : 0;
}

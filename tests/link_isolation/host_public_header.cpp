#include <draxul/host.h>

int main()
{
    auto host = draxul::create_host(draxul::HostKind::Nvim);
    return host ? 0 : 0;
}

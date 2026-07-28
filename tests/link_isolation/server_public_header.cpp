#include <draxul/server_kernel.h>

int main()
{
    draxul::ServerKernel server({
        .runtime_directory = {},
    });
    return server.running() ? 1 : 0;
}

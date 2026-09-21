#include <draxul/nvim_transport.h>

int main()
{
    draxul::NvimProcess process;
    return process.is_running() ? 1 : 0;
}

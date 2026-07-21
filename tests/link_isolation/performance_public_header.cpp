#include <draxul/perf_timing.h>

int main()
{
    return draxul::runtime_perf_collector().enabled() ? 0 : 0;
}

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <draxul/log.h>

int main(int argc, char* argv[])
{
    draxul::configure_logging();
    return Catch::Session().run(argc, argv);
}

#include "support/test_support.h"

#include <draxul/log.h>
#include <draxul/nvim_rpc.h>

#include <catch2/catch_all.hpp>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string missing_nvim_path()
{
#ifdef _WIN32
    return "C:\\definitely-missing\\draxul-test-nvim.exe";
#else
    return "/definitely-missing/draxul-test-nvim";
#endif
}

bool contains_spawn_error(const std::vector<LogRecord>& records)
{
    for (const auto& record : records)
    {
        if (record.category == LogCategory::Nvim
            && record.level == LogLevel::Error
            && record.message.find("Failed to spawn nvim") != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("nvim process spawn returns false and logs an error for a bad path", "[nvim]")
{
    ScopedLogCapture capture;
    NvimProcess process;

    INFO("spawn should fail for a missing nvim binary");
    REQUIRE(!process.spawn(missing_nvim_path()));
    INFO("failed spawn should not leave a running process");
    REQUIRE(!process.is_running());
    INFO("failed spawn should emit an error log");
    REQUIRE(contains_spawn_error(capture.records));
}

TEST_CASE("nvim process shutdown is a no-op after spawn failure", "[nvim]")
{
    NvimProcess process;

    INFO("spawn should fail for a missing nvim binary");
    REQUIRE(!process.spawn(missing_nvim_path()));
    process.shutdown();
    INFO("shutdown after failed spawn should remain safe");
    REQUIRE(!process.is_running());
}

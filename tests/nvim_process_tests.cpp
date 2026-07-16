#include "support/test_support.h"
#include "support/scoped_env_var.h"

#include <draxul/log.h>
#include <draxul/nvim_rpc.h>

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <latch>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format on
#endif

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

#ifdef _WIN32
TEST_CASE("Windows nvim process status is safe during concurrent shutdown", "[nvim][windows]")
{
    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;

    // Warm up CreateProcess and the fake RPC helper before taking the process
    // handle baseline. This keeps one-time loader/runtime handles out of the
    // ownership check below.
    REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));
    process.shutdown();
    DWORD handle_count_before = 0;
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &handle_count_before));

    // Reusing one process wrapper also verifies that shutdown clears all
    // published process identity before the next spawn.
    for (int cycle = 0; cycle < 16; ++cycle)
    {
        INFO("cycle " << cycle);
        REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));
        REQUIRE(process.is_running());

        std::latch start{ 2 };
        std::latch first_status_check{ 1 };
        std::atomic<bool> shutdown_complete{ false };
        std::atomic<bool> post_shutdown_all_stopped{ true };
        std::atomic<size_t> status_checks{ 0 };
        std::thread observer([&]() {
            start.count_down();
            start.wait();
            static_cast<void>(process.is_running());
            status_checks.fetch_add(1, std::memory_order_relaxed);
            first_status_check.count_down();
            while (!shutdown_complete.load(std::memory_order_acquire))
            {
                static_cast<void>(process.is_running());
                status_checks.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }

            // Keep querying after the handle has been unpublished and closed.
            for (int i = 0; i < 256; ++i)
            {
                if (process.is_running())
                    post_shutdown_all_stopped.store(false, std::memory_order_relaxed);
            }
        });

        start.count_down();
        start.wait();
        first_status_check.wait();
        process.shutdown();
        shutdown_complete.store(true, std::memory_order_release);
        observer.join();

        REQUIRE(status_checks.load(std::memory_order_relaxed) > 0);
        REQUIRE(post_shutdown_all_stopped.load(std::memory_order_relaxed));
        REQUIRE_FALSE(process.is_running());
        process.shutdown(); // Repeated shutdown must remain a no-op.
    }

    DWORD handle_count_after = 0;
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &handle_count_after));
    INFO("process handles before=" << handle_count_before << " after=" << handle_count_after);
    // Allow a very small amount of unrelated test/runtime noise while still
    // making a leaked process/thread/pipe set fail deterministically.
    CHECK(handle_count_after <= handle_count_before + 2);
}

TEST_CASE("Windows nvim process reports an already-exited child as stopped", "[nvim][windows]")
{
    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "dump_term_and_exit");
    NvimProcess process;
    REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (process.is_running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    REQUIRE_FALSE(process.is_running());
    process.shutdown();
    REQUIRE_FALSE(process.is_running());
}
#endif

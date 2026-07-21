#include "self_launch.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace draxul;

TEST_CASE("self launch argv preserves exact caller arguments", "[session][process]")
{
#ifdef _WIN32
    const std::filesystem::path executable = L"C:\\Program Files\\Dräxul\\draxul.exe";
#else
    const std::filesystem::path executable = "/Applications/Dräxul App/draxul";
#endif
    const auto command = make_self_launch_command(executable,
        { "--session", "name with spaces", "--session-name", "雪", "" });

    REQUIRE(command.executable_path == executable);
    REQUIRE(command.argv.size() == 6);
    CHECK(command.argv[0] == executable.string());
    CHECK(command.argv[1] == "--session");
    CHECK(command.argv[2] == "name with spaces");
    CHECK(command.argv[3] == "--session-name");
    CHECK(command.argv[4] == "雪");
    CHECK(command.argv[5].empty());
}

TEST_CASE("self launch reports the failing process API", "[session][process]")
{
    const auto result = launch_self_process(
        make_self_launch_command("this-path-must-not-exist/draxul", {}));
    REQUIRE_FALSE(result.launched());
#ifdef _WIN32
    CHECK(result.error_api == SelfLaunchErrorApi::CreateProcessW);
    CHECK(result.error_message().find("CreateProcessW failed:") == 0);
#else
    CHECK(result.error_api == SelfLaunchErrorApi::PosixSpawn);
    CHECK(result.error_message().find("posix_spawn() failed:") == 0);
#endif
    CHECK(result.error_code != 0);
}

TEST_CASE("self launch starts a harmless helper while worker threads are active", "[session][process]")
{
    std::atomic<bool> stop = false;
    std::vector<std::thread> workers;
    for (int index = 0; index < 4; ++index)
    {
        workers.emplace_back([&stop] {
            std::vector<std::string> allocations;
            while (!stop.load(std::memory_order_relaxed))
            {
                allocations.emplace_back("allocator activity");
                if (allocations.size() > 32)
                    allocations.clear();
                std::this_thread::yield();
            }
        });
    }

#ifdef _WIN32
    const char* command_processor = std::getenv("COMSPEC");
    REQUIRE(command_processor != nullptr);
    const auto command = make_self_launch_command(command_processor, { "/D", "/C", "exit", "0" });
#else
    const auto command = make_self_launch_command("/usr/bin/true", {});
#endif
    const auto result = launch_self_process(command);
    stop.store(true, std::memory_order_relaxed);
    for (auto& worker : workers)
        worker.join();

    INFO(result.error_message());
    CHECK(result.launched());
}

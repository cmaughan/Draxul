#ifdef _WIN32

#include <catch2/catch_all.hpp>

#include "../libs/draxul-host/src/conpty_process.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using namespace draxul;

TEST_CASE("ConPtyProcess reports child process working directory changes", "[conpty_process]")
{
    const auto root = std::filesystem::temp_directory_path() / "draxul-conpty-cwd";
    const auto child = root / "child";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(child);

    ConPtyProcess process;
    REQUIRE(process.spawn("cmd.exe", { "/Q", "/K" }, root.string(), 80, 24, [] {}));

    auto process_observation = process.foreground_process_observation();
    REQUIRE(process_observation);
    CHECK_FALSE(process_observation->foreground_reliable);
    const auto command = std::find_if(process_observation->processes.begin(),
        process_observation->processes.end(), [](const AgentProcessInfo& info) {
            return std::filesystem::path(info.executable).filename()
                == "cmd.exe";
        });
    REQUIRE(command != process_observation->processes.end());
    CHECK(std::find(command->arguments.begin(), command->arguments.end(), "/Q")
        != command->arguments.end());

    std::string last_seen_cwd;
    auto wait_for_cwd = [&process, &last_seen_cwd](const std::filesystem::path& expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            last_seen_cwd = process.current_working_directory();
            std::error_code ec;
            if (!last_seen_cwd.empty() && std::filesystem::equivalent(last_seen_cwd, expected, ec))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };

    const bool saw_root = wait_for_cwd(root);
    INFO("last seen cwd: " << last_seen_cwd);
    INFO("expected cwd: " << root.string());
    REQUIRE(saw_root);

    REQUIRE(process.write("cd /d child\r"));
    const bool saw_child = wait_for_cwd(child);
    INFO("last seen cwd: " << last_seen_cwd);
    INFO("expected cwd: " << child.string());
    REQUIRE(saw_child);

    process.shutdown();
    std::filesystem::remove_all(root);
}

#endif

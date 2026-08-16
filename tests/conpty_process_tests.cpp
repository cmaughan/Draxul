#ifdef _WIN32

#include <catch2/catch_all.hpp>

#include <draxul/agent_model.h>
#include <draxul/conpty_process.h>

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
    REQUIRE(process.spawn("cmd.exe", { "/Q", "/K" },
        root.string(), 80, 24, [] {}));

    std::optional<AgentProcessObservation> process_observation;
    const auto observation_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!process_observation
        && std::chrono::steady_clock::now()
            < observation_deadline)
    {
        process_observation
            = process.foreground_process_observation();
        if (!process_observation)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
    }
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

    std::string output;
    const auto prompt_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (output.find('>') == std::string::npos
        && std::chrono::steady_clock::now()
            < prompt_deadline)
    {
        for (auto& chunk : process.drain_output())
            output += chunk;
        if (output.find('>') == std::string::npos)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
    }
    REQUIRE(output.find('>') != std::string::npos);

    std::string last_seen_cwd;
    auto wait_for_cwd = [&process, &last_seen_cwd](const std::filesystem::path& expected) {
        return draxul::tests::wait_until(
            [&] {
                last_seen_cwd = process.current_working_directory();
                std::error_code ec;
                return !last_seen_cwd.empty()
                    && std::filesystem::equivalent(last_seen_cwd, expected, ec);
            },
            std::chrono::seconds(3));
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

    const auto system_directory
        = std::filesystem::path(
              std::getenv("SystemRoot"))
        / "System32";
    const auto fake_agent = root / "codex.exe";
    std::filesystem::copy_file(
        system_directory / "cmd.exe", fake_agent,
        std::filesystem::copy_options::overwrite_existing);
    REQUIRE(process.write("\"" + fake_agent.string()
        + "\" /Q /C ping -n 4 127.0.0.1 >nul\r"));
    bool detected_child = false;
    const auto child_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (!detected_child
        && std::chrono::steady_clock::now()
            < child_deadline)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
        process_observation
            = process.foreground_process_observation();
        detected_child = process_observation
            && std::ranges::any_of(
                process_observation->processes,
                [](const AgentProcessInfo& info) {
                    return std::filesystem::path(
                               info.executable)
                               .filename()
                        == "codex.exe";
                });
    }
    REQUIRE(detected_child);
    REQUIRE(process_observation);
    const auto discovered
        = discover_agent_process(*process_observation);
    REQUIRE(discovered);
    CHECK(discovered->kind == "codex");

    bool child_exited = false;
    const auto exit_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(6);
    while (!child_exited
        && std::chrono::steady_clock::now()
            < exit_deadline)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
        process_observation
            = process.foreground_process_observation();
        child_exited = process_observation
            && std::ranges::none_of(
                process_observation->processes,
                [](const AgentProcessInfo& info) {
                    return std::filesystem::path(
                               info.executable)
                               .filename()
                        == "codex.exe";
                });
    }
    REQUIRE(child_exited);

    process.shutdown();
    std::filesystem::remove_all(root);
}

#endif

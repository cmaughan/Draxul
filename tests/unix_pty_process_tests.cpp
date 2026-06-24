#ifndef _WIN32

#include <catch2/catch_all.hpp>

#include "../libs/draxul-host/src/unix_pty_process.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace draxul;

TEST_CASE("UnixPtyProcess sets xterm-256color shell environment", "[unix_pty_process]")
{
    const auto dump_path
        = std::filesystem::temp_directory_path() / "draxul-unix-pty-env-dump.txt";
    std::filesystem::remove(dump_path);

    UnixPtyProcess process;
    const std::string script = "printf '%s\\n%s\\n%s\\n' \"$TERM\" \"$COLORTERM\" \"$TERM_PROGRAM\" > '"
        + dump_path.string() + "'";
    REQUIRE(process.spawn("/bin/sh", { "-c", script }, "", [] {}, 80, 24));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (process.is_running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    process.shutdown();

    std::ifstream in(dump_path);
    REQUIRE(in.good());

    std::string term;
    std::string colorterm;
    std::string term_program;
    std::getline(in, term);
    std::getline(in, colorterm);
    std::getline(in, term_program);

    REQUIRE(term == "xterm-256color");
    REQUIRE(colorterm == "truecolor");
    REQUIRE(term_program == "draxul");

    std::filesystem::remove(dump_path);
}

TEST_CASE("UnixPtyProcess reports child process working directory changes", "[unix_pty_process]")
{
    const auto root = std::filesystem::temp_directory_path() / "draxul-unix-pty-cwd";
    const auto child = root / "child";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(child);

    UnixPtyProcess process;
    REQUIRE(process.spawn("/bin/sh", {}, root.string(), [] {}, 80, 24));

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

    REQUIRE(process.write("cd child\n"));
    const bool saw_child = wait_for_cwd(child);
    INFO("last seen cwd: " << last_seen_cwd);
    INFO("expected cwd: " << child.string());
    REQUIRE(saw_child);

    process.shutdown();
    std::filesystem::remove_all(root);
}

#endif

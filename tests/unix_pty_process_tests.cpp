#ifndef _WIN32

#include <catch2/catch_all.hpp>

#include <draxul/unix_pty_process.h>

#include <draxul/agent_model.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
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

TEST_CASE("UnixPtyProcess observes an agent started by hand inside the shell",
    "[unix_pty_process][agent][discovery]")
{
    // The reported symptom end to end: open a shell pane, type the agent's
    // name, expect it to be recognised. This exercises the half that unit
    // tests could not reach -- tcgetpgrp() on the PTY master plus the
    // per-process lookup -- against a real interactive shell, and then feeds
    // the result through the same matcher the AgentController uses.
    const auto root = std::filesystem::temp_directory_path() / "dxl-agent-observe";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // Reproduce the REAL native install layout, not a conveniently-named
    // stand-in: the binary lives in a version-numbered file and is reached
    // through a `claude` symlink. exec() resolves the symlink, so the kernel
    // reports ".../versions/9.9.9" and the executable basename is a version
    // number — which is exactly what defeated detection. `cat` blocks on the
    // terminal, which is what an interactive agent looks like to us.
    const auto versions = root / "share" / "claude" / "versions";
    std::filesystem::create_directories(versions);
    const auto versioned = versions / "9.9.9";
    std::filesystem::copy_file("/bin/cat", versioned);
    std::filesystem::permissions(versioned,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);

    const auto bin = root / "bin";
    std::filesystem::create_directories(bin);
    const auto fake_agent = bin / "claude";
    std::filesystem::create_symlink(versioned, fake_agent);

    // zsh, because that is what Draxul actually spawns on macOS and because a
    // job-control shell puts the command in its OWN foreground process group —
    // which is precisely what tcgetpgrp() has to resolve.
    // Point zsh at an EMPTY config directory. A login shell would otherwise
    // source the developer's real rc files, whose cost (plugin managers,
    // version managers) varies wildly and made this test's timing depend on
    // whoever happened to run it.
    const auto zdotdir = root / "zdot";
    std::filesystem::create_directories(zdotdir);

    UnixPtyProcess process;
    REQUIRE(process.spawn("/bin/zsh", {}, root.string(), [] {}, 80, 24,
        /*login_shell=*/true, { { "ZDOTDIR", zdotdir.string() } }));

    // Anything written before the shell starts reading is lost, and there is
    // no reliable way to observe "zsh is ready" from outside. Rather than
    // guess at a settling delay — which is what made this flake under parallel
    // shards — re-send the command while polling, so a write that lands too
    // early simply gets repeated. Re-typing while the agent already runs just
    // feeds its stdin, which is harmless.
    std::optional<AgentDiscoveryMatch> match;
    std::optional<AgentProcessObservation> last;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto next_write = std::chrono::steady_clock::now();
    while (!match && std::chrono::steady_clock::now() < deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_write)
        {
            process.write(fake_agent.string() + "\n");
            next_write = now + std::chrono::milliseconds(750);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        last = process.foreground_process_observation();
        if (last)
            match = discover_agent_process(*last);
    }

    std::string detail = last ? "observation present" : "no observation";
    if (last)
    {
        detail += ", foreground_reliable="
            + std::string(last->foreground_reliable ? "true" : "false");
        detail += ", processes=" + std::to_string(last->processes.size());
        for (const auto& entry : last->processes)
        {
            detail += "\n  pid=" + std::to_string(entry.process_id)
                + " ppid=" + std::to_string(entry.parent_process_id)
                + " exe='" + entry.executable + "'"
                + " hint='" + entry.agent_hint + "'";
            for (const auto& arg : entry.arguments)
                detail += " arg='" + arg + "'";
        }
    }
    INFO(detail);
    REQUIRE(match);
    CHECK(match->kind == "claude");
    CHECK(match->evidence_category == "direct_executable");
    CHECK(match->high_confidence);

    process.shutdown();
    std::filesystem::remove_all(root);
}

#endif

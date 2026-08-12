// Regression guard for WI 26 (cli-numeric-arg-crash). Ensures the CLI parser
// rejects malformed numeric flags with a useful error rather than crashing
// or silently accepting them, and confirms that valid numeric flags parse.

#include <catch2/catch_test_macros.hpp>

#include "cli_args.h"
#include "cli_help.h"

#include <string>
#include <vector>

using namespace draxul;

namespace
{

ParseArgsResult parse(std::initializer_list<const char*> tokens)
{
    std::vector<std::string> args;
    args.emplace_back("draxul"); // argv[0]
    for (const char* t : tokens)
        args.emplace_back(t);
    return parse_args(args);
}

} // namespace

TEST_CASE("cli: --screenshot-delay with non-numeric value reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "abc" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-delay") != std::string::npos);
}

TEST_CASE("cli: --screenshot-delay with negative value reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "-1" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-delay") != std::string::npos);
}

TEST_CASE("cli: --screenshot-delay with trailing garbage reports an error", "[cli]")
{
    // Without strict end-of-input parsing, "100x" would be silently truncated.
    auto r = parse({ "--screenshot-delay", "100x" });
    REQUIRE(r.error.has_value());
}

TEST_CASE("cli: --screenshot-delay with valid value parses cleanly", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "100" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 100);
}

TEST_CASE("cli: --screenshot-delay zero is accepted (non-negative)", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "0" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 0);
}

TEST_CASE("cli: --screenshot-size with zero dimension reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "0x600" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-size") != std::string::npos);
}

TEST_CASE("cli: --screenshot-size with non-numeric dimensions reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "xyz" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-size") != std::string::npos);
}

TEST_CASE("cli: --screenshot-size with garbled width reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "abcx600" });
    REQUIRE(r.error.has_value());
}

TEST_CASE("cli: --screenshot-size with valid value parses both dimensions", "[cli]")
{
    auto r = parse({ "--screenshot-size", "1024x768" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_width == 1024);
    REQUIRE(r.args.screenshot_height == 768);
}

TEST_CASE("cli: no args produces default ParsedArgs without error", "[cli]")
{
    auto r = parse({});
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 6000);
    REQUIRE(r.args.screenshot_width == 0);
    REQUIRE(r.args.screenshot_height == 0);
    REQUIRE_FALSE(r.args.smoke_test);
    REQUIRE(should_use_shared_server(r.args));
}

TEST_CASE("cli: --smoke-test sets the flag", "[cli]")
{
    auto r = parse({ "--smoke-test" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.smoke_test);
}

TEST_CASE("cli: --pty-capture-file stores the requested capture path", "[cli]")
{
    auto r = parse({ "--pty-capture-file", "D:/tmp/capture.log" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.pty_capture_file == "D:/tmp/capture.log");
}

TEST_CASE("cli: --session stores the requested session id", "[cli]")
{
    auto r = parse({ "--session", "workbench" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.session_id == "workbench");
}

TEST_CASE("cli: --session-name stores the requested session name", "[cli]")
{
    auto r = parse({ "--session-name", "Work Bench" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.session_name == "Work Bench");
}

TEST_CASE("cli: --list-sessions sets the flag", "[cli]")
{
    auto r = parse({ "--list-sessions" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.list_sessions);
}

TEST_CASE("cli: --new-session sets the flag", "[cli]")
{
    auto r = parse({ "--new-session" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.new_session);
}

TEST_CASE("cli: --rename-session sets the flag", "[cli]")
{
    auto r = parse({ "--rename-session", "--session-name", "Renamed" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.rename_session);
}

TEST_CASE("cli: --rename-session without --session-name reports an error", "[cli]")
{
    auto r = parse({ "--rename-session" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--rename-session") != std::string::npos);
}

TEST_CASE("cli: --delete-session sets the flag", "[cli]")
{
    auto r = parse({
        "--delete-session", "--session", "work",
    });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.delete_session);
    REQUIRE(r.args.session_id == "work");
}

TEST_CASE("cli: --delete-session requires an explicit Session",
    "[cli][server]")
{
    auto r = parse({ "--delete-session" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--session <id>")
        != std::string::npos);
}

TEST_CASE("cli: --delete-all-sessions requires confirmation",
    "[cli][server]")
{
    auto r = parse({ "--delete-all-sessions" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--yes") != std::string::npos);

    r = parse({ "--delete-all-sessions", "--yes" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.delete_all_sessions);
    REQUIRE(r.args.confirmed);

    r = parse({
        "--delete-all-sessions", "--yes",
        "--session", "work",
    });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--session")
        != std::string::npos);
}

TEST_CASE("cli: session control modes are mutually exclusive", "[cli]")
{
    auto r = parse({ "--new-session", "--rename-session",
        "--session-name", "Renamed" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("choose only one") != std::string::npos);
}

TEST_CASE("cli: Session listing is a server control mode",
    "[cli][server]")
{
    auto r = parse({ "--list-sessions", "--server-status" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("choose only one") != std::string::npos);

    r = parse({ "--list-sessions", "--new-session" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("Session-control") != std::string::npos);
}

TEST_CASE("cli: server control modes are mutually exclusive", "[cli][server]")
{
    auto r = parse({ "--server", "--server-status" });
    REQUIRE(r.error.has_value());

    r = parse({
        "--list-sessions", "--delete-session",
        "--session", "work",
    });
    REQUIRE(r.error.has_value());

    r = parse({
        "--delete-session", "--delete-all-sessions",
        "--session", "work", "--yes",
    });
    REQUIRE(r.error.has_value());
}

TEST_CASE("cli: server runtime override is parsed", "[cli][server]")
{
    auto r = parse({ "--server", "--server-runtime-dir", "D:/tmp/draxul-server" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.server);
    REQUIRE(r.args.server_runtime_dir == "D:/tmp/draxul-server");
}

TEST_CASE("cli: server owns remote shell launch and history settings",
    "[cli][server][remote-terminal]")
{
    auto r = parse({
        "--experimental-remote-shell",
        "--server-shell",
        "zsh",
        "--server-working-dir",
        "D:/work/project",
        "--server-scrollback-lines",
        "25000",
    });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.server_shell_kind == "zsh");
    REQUIRE(r.args.server_working_dir == "D:/work/project");
    REQUIRE(r.args.server_scrollback_lines == 25000);

    REQUIRE(parse({ "--server", "--server-shell", "fish" })
            .error.has_value());
    REQUIRE(parse({ "--server", "--server-scrollback-lines", "-1" })
            .error.has_value());
    REQUIRE(parse({ "--server", "--server-scrollback-lines", "many" })
            .error.has_value());
}

TEST_CASE("cli: experimental bootstrap follows shared-server startup modes", "[cli][server]")
{
    auto normal = parse({ "--experimental-server-client" });
    REQUIRE(should_bootstrap_experimental_server(normal.args));

    auto explicit_host = parse(
        { "--experimental-server-client", "--host", "powershell" });
    REQUIRE(should_bootstrap_experimental_server(explicit_host.args));

    auto smoke = parse(
        { "--experimental-server-client", "--smoke-test" });
    REQUIRE(should_bootstrap_experimental_server(smoke.args));

}

TEST_CASE("cli: normal shell startup uses the shared server by default",
    "[cli][server][slice9]")
{
    REQUIRE(should_use_shared_server(parse({}).args));
    REQUIRE(should_use_shared_server(
        parse({ "--host", "powershell" }).args));
    REQUIRE(should_use_shared_server(
        parse({ "--host", "zsh" }).args));

    REQUIRE(parse({ "--no-server" }).error);
    REQUIRE_FALSE(should_use_shared_server(
        parse({ "--host", "nvim" }).args));
    REQUIRE(parse({ "--host", "satview" }).error);
    REQUIRE(parse({ "--host", "score" }).error);
    REQUIRE(should_use_shared_server(
        parse({ "--smoke-test" }).args));
}

TEST_CASE("cli: server stop confirmation is explicit",
    "[cli][server][slice9]")
{
    auto graceful = parse({ "--shutdown-server" });
    REQUIRE_FALSE(graceful.error);
    REQUIRE_FALSE(graceful.args.confirmed);

    auto confirmed
        = parse({ "--shutdown-server", "--yes" });
    REQUIRE_FALSE(confirmed.error);
    REQUIRE(confirmed.args.confirmed);

    REQUIRE(parse({ "--force-stop-server" }).error);
    auto forced
        = parse({ "--force-stop-server", "--yes" });
    REQUIRE_FALSE(forced.error);
    REQUIRE(forced.args.force_stop_server);
    REQUIRE(forced.args.confirmed);

    auto dialog = parse({
        "--server-stop-dialog",
        "--server-runtime-dir", "D:/runtime",
    });
    REQUIRE_FALSE(dialog.error);
    REQUIRE(dialog.args.server_stop_dialog);
    REQUIRE_FALSE(should_use_shared_server(dialog.args));
    REQUIRE(parse({
        "--server-stop-dialog", "--server-status",
    }).error);

    auto delete_session = parse({
        "--delete-session", "--session", "work", "--yes",
    });
    REQUIRE_FALSE(delete_session.error);
    REQUIRE(delete_session.args.delete_session);
    REQUIRE(delete_session.args.confirmed);
    REQUIRE(parse({ "--yes" }).error);
}

TEST_CASE("cli: help and server command are parsed",
    "[cli][server][slice9]")
{
    auto help = parse({ "--help" });
    REQUIRE_FALSE(help.error);
    REQUIRE(help.args.help);
    REQUIRE_FALSE(should_use_shared_server(help.args));

    auto command = parse({
        "--server",
        "--server-command",
        "D:/tools/pwsh.exe",
    });
    REQUIRE_FALSE(command.error);
    REQUIRE(command.args.server_command
        == "D:/tools/pwsh.exe");
}

TEST_CASE("cli: help enumerates public command families", "[cli][help]")
{
    const std::string help = cli_help_text();

    REQUIRE(help.find("draxul --server-status") != std::string::npos);
    REQUIRE(help.find("draxul --list-sessions") != std::string::npos);
    REQUIRE(help.find("draxul space list") != std::string::npos);
    REQUIRE(help.find("draxul agent start") != std::string::npos);
    REQUIRE(help.find("draxul agent wait") != std::string::npos);
    REQUIRE(help.find("draxul pane read") != std::string::npos);
    REQUIRE(help.find("draxul integration status") != std::string::npos);
    REQUIRE(help.find("--force-stop-server") == std::string::npos);
    REQUIRE(help.find("--server-stop-dialog") == std::string::npos);
}

TEST_CASE("cli: fake remote terminal opts into server bootstrap",
    "[cli][server][remote-terminal]")
{
    auto remote = parse({ "--experimental-remote-terminal" });
    REQUIRE_FALSE(remote.error.has_value());
    REQUIRE(remote.args.experimental_remote_terminal);
    REQUIRE(remote.args.experimental_server_client);
    REQUIRE(should_bootstrap_experimental_server(remote.args));

    REQUIRE(parse({ "--experimental-remote-terminal",
                       "--host", "powershell" })
            .error.has_value());
    REQUIRE(parse({ "--experimental-remote-terminal", "--smoke-test" })
            .error.has_value());
}

TEST_CASE("cli: real remote shell opts into server bootstrap",
    "[cli][server][remote-terminal]")
{
    auto remote = parse({ "--experimental-remote-shell" });
    REQUIRE_FALSE(remote.error.has_value());
    REQUIRE(remote.args.experimental_remote_shell);
    REQUIRE(remote.args.experimental_server_client);
    REQUIRE(should_bootstrap_experimental_server(remote.args));

    REQUIRE(parse({ "--experimental-remote-shell",
                       "--host", "powershell" })
            .error.has_value());
    REQUIRE(parse({ "--experimental-remote-shell", "--smoke-test" })
            .error.has_value());
    REQUIRE(parse({ "--experimental-remote-shell",
                       "--experimental-remote-terminal" })
            .error.has_value());
}

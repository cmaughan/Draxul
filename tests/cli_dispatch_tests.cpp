#include <catch2/catch_test_macros.hpp>
#include <draxul/cli_dispatch.h>

#include "support/scoped_env_var.h"
#include "support/temp_dir.h"
#include <draxul/executable_layout.h>
#include <draxul/launch_policy.h>
#include <draxul/server_kernel.h>
#include <draxul/session_id.h>
#include <memory>
#include <sstream>
#include <thread>

#include <draxul/server_status_text.h>

using namespace draxul;

TEST_CASE("control CLI recognizes read-only Space and pane commands", "[control][cli]")
{
    auto spaces = parse_control_cli(
        { "draxul", "space", "list", "--session", "work", "--json" });
    REQUIRE(spaces.command);
    CHECK(spaces.command->method == "space.list");
    CHECK(spaces.command->session_id == "work");
    CHECK(spaces.command->json);

    auto pane = parse_control_cli({ "draxul", "pane", "read", "pane-4", "--lines", "25" });
    REQUIRE(pane.command);
    CHECK(pane.command->method == "pane.read");
    CHECK(pane.command->value == "pane-4");
    CHECK(pane.command->lines == 25);

    auto invalid = parse_control_cli({ "draxul", "pane", "read", "pane-4", "--lines", "201" });
    CHECK(invalid.recognized);
    CHECK(invalid.error);

    auto reload = parse_control_cli({
        "draxul", "plugin", "reload", "dev.draxul.fixture", "--json" });
    REQUIRE(reload.command);
    CHECK(reload.command->method == "plugin.reload");
    CHECK(reload.command->value == "dev.draxul.fixture");
    CHECK(reload.command->json);

    auto focus = parse_control_cli(
        { "draxul", "pane", "focus", "pane-9", "--session", "work" });
    REQUIRE(focus.command);
    CHECK(focus.command->method == "pane.focus");
    CHECK(focus.command->value == "pane-9");

    auto action = parse_control_cli({ "draxul", "pane", "action", "pane-9",
        "--action", "rezonality_reload", "--json" });
    REQUIRE(action.command);
    CHECK(action.command->method == "pane.action");
    CHECK(action.command->action == "rezonality_reload");
    CHECK(action.command->json);

    auto routed_focus = parse_control_cli({ "draxul", "pane", "focus",
        "pane-9", "--ui", "ui-window-2", "--json" });
    REQUIRE(routed_focus.command);
    CHECK(routed_focus.command->control_id == "ui-window-2");
    CHECK(routed_focus.command->control_id_explicit);

    auto uis = parse_control_cli(
        { "draxul", "ui", "list", "--session", "work", "--json" });
    REQUIRE(uis.command);
    CHECK(uis.command->method == "ui.list");
    CHECK(uis.command->session_id == "work");

    auto missing_action = parse_control_cli(
        { "draxul", "pane", "action", "pane-9" });
    CHECK(missing_action.error);
}

TEST_CASE("control CLI keeps agent argv structured and parses wait policy", "[control][cli]")
{
    auto start = parse_control_cli({ "draxul", "agent", "start", "codex",
        "--cwd", "D:/work", "--space", "2", "--", "--model", "gpt-5" });
    REQUIRE(start.command);
    CHECK(start.command->method == "agent.start");
    CHECK(start.command->working_directory == "D:/work");
    CHECK(start.command->space_id == 2);
    CHECK(start.command->arguments
        == std::vector<std::string>{ "--model", "gpt-5" });

    auto wait = parse_control_cli({ "draxul", "agent", "wait", "agent-4",
        "--until", "blocked,done", "--timeout", "10m" });
    REQUIRE(wait.command);
    CHECK(wait.command->timeout_ms == 10 * 60 * 1000);
    CHECK(wait.command->values
        == std::vector<std::string>{ "blocked", "done" });

    auto report = parse_control_cli({
        "draxul",
        "pane",
        "report-agent-session",
        "pane-7",
        "--agent-instance",
        "agent-7",
        "--source",
        "draxul:codex",
        "--agent",
        "codex",
        "--integration-version",
        "1",
        "--sequence",
        "9",
        "--session-ref",
        "native-7",
        "--server-epoch",
        "epoch-7",
        "--runtime-generation",
        "3",
        "--server-runtime-dir",
        "D:/runtime",
    });
    REQUIRE(report.command);
    CHECK(report.command->server_epoch == "epoch-7");

    // Hand-started agents report without a managed instance id.
    auto shell_report = parse_control_cli({ "draxul", "pane", "report-agent-session", "pane-7",
        "--source", "draxul:claude", "--agent", "claude", "--integration-version", "3",
        "--sequence", "9", "--session-ref", "native-7", "--ref-kind", "id" });
    REQUIRE(shell_report.command);
    CHECK(shell_report.command->agent_instance_id.empty());
    CHECK(report.command->runtime_generation == 3);
    CHECK(report.command->server_runtime_directory
        == "D:/runtime");
}


TEST_CASE("integration CLI supports both official native session hooks",
    "[control][integration]")
{
    auto status = parse_integration_cli({ "draxul", "integration", "status", "--json" });
    REQUIRE(status.command);
    CHECK(status.command->target.empty());
    CHECK(status.command->json);

    auto claude = parse_integration_cli({ "draxul", "integration", "install", "claude" });
    REQUIRE(claude.command);
    CHECK(claude.command->target == "claude");

    auto invalid = parse_integration_cli({ "draxul", "integration", "install", "other" });
    CHECK(invalid.recognized);
    CHECK(invalid.error);
}


namespace
{
struct CliCapture
{
    std::unique_ptr<FILE, decltype(&std::fclose)> output{ std::tmpfile(), &std::fclose };
    std::unique_ptr<FILE, decltype(&std::fclose)> error{ std::tmpfile(), &std::fclose };
    std::istringstream input;
    CliContext context;

    explicit CliCapture(std::string text = {}) : input(std::move(text))
    {
        REQUIRE(output);
        REQUIRE(error);
        context.output = output.get();
        context.error = error.get();
        context.input = &input;
    }

    static std::string read(FILE* file)
    {
        std::fflush(file);
        std::rewind(file);
        std::string text;
        char buffer[1024];
        while (const auto count = std::fread(buffer, 1, sizeof(buffer), file))
            text.append(buffer, count);
        return text;
    }
};

struct RunningCliServer
{
    ServerKernel& server;
    std::jthread thread;
    explicit RunningCliServer(ServerKernel& value)
        : server(value), thread([&value] { value.run_until_stopped(); }) {}
    ~RunningCliServer() { server.request_stop(); }
};
}

TEST_CASE("CLI dispatch drives a real server with quoted names and isolated IO", "[cli][integration]")
{
    tests::TempDir temporary("cli");
    tests::ScopedEnvVar inherited_session("DRAXUL_SESSION_ID", "wrong-session");
    tests::ScopedEnvVar inherited_runtime("DRAXUL_SERVER_RUNTIME_DIR", "missing-runtime");
    const auto runtime = temporary.path / "runtime";
    ServerKernel server({ .runtime_directory = runtime, .build_version = "cli-test" });
    const auto started = server.start();
    INFO(started.error);
    REQUIRE(started.disposition == ServerStartDisposition::Started);
    RunningCliServer running(server);
    auto invoke = [&](std::vector<std::string> args, std::string input = {}, int expected = 0) {
        args.insert(args.begin(), "draxul");
        args.insert(args.end(), { "--server-runtime-dir", runtime.string(), "--session", "cli-session", "--json" });
        auto command = parse_command_line(args);
        INFO(command.error.value_or(""));
        REQUIRE_FALSE(command.error);
        CliCapture capture(std::move(input));
        const auto exit_code = dispatch_cli(command, capture.context);
        const auto error = CliCapture::read(capture.error.get());
        INFO(error);
        REQUIRE(exit_code == expected);
        return nlohmann::json::parse(expected == 0 ? CliCapture::read(capture.output.get()) : error);
    };
    const std::string name = "Flight deck \"quoted\" \xE9\x9B\xAA";
    const auto created = invoke({ "space", "create", "--name", name });
    REQUIRE(created.at("ok") == true);
    const auto space_id = created.at("created_id").get<std::string>();
    const auto space = invoke({ "space", "get", space_id });
    CHECK(space.at("name") == name);
    const auto tab = invoke({ "tab", "create", "--space", space_id,
        "--name", name, "--plugin", "dev.draxul.spinning-triangle" });
    REQUIRE(tab.at("ok") == true);
    const auto tab_id = tab.at("created_id").get<std::string>();
    CHECK(invoke({ "tab", "get", tab_id }).at("name") == name);
    const auto invalid_layout = invoke({ "layout", "validate", "-" }, "not JSON", 1);
    CHECK(invalid_layout.at("error").at("code") == "invalid_json");
    const auto missing = invoke({ "pane", "get", "missing-pane" }, {}, 1);
    CHECK(missing.at("error").at("code") == "pane_not_found");

    const auto probe = ServerClient::probe({ .runtime_directory = runtime,
        .client_id = "cli-policy-test", .launch_if_missing = false });
    REQUIRE(probe.ready());
    auto parsed = parse_args({ "draxul", "--new-session", "--session-name", name }).args;
    CHECK_FALSE(validate_server_launch_capabilities(parsed, *probe.welcome));
    auto unsupported = *probe.welcome;
    std::erase(unsupported.capabilities, "topology-v1");
    REQUIRE(validate_server_launch_capabilities(parsed, unsupported));
    CHECK(validate_server_launch_capabilities(parsed, unsupported)->find("shared topology") != std::string::npos);
    std::string disconnect_error;
    REQUIRE(ServerClient::disconnect(runtime, "cli-policy-test", disconnect_error, probe.welcome->connection_token));
    const auto status = ServerClient::status(runtime);
    REQUIRE(status.ok);
    REQUIRE(status.status);
    parsed.session_id = "cli-session";
    parsed.session_id_explicit = true;
    REQUIRE(resolve_new_session_id(parsed, *status.status, 123));
    parsed.session_id_explicit = false;
    CHECK_FALSE(resolve_new_session_id(parsed, *status.status, 123));
    CHECK(parsed.session_id == make_session_id_base(name, 123));
    CHECK(invoke({ "space", "close", space_id }).at("ok") == true);
}

TEST_CASE("CLI dispatch preserves precedence exit codes and request arguments", "[cli][integration]")
{
    SECTION("malformed topology uses its own grammar and exit code")
    {
        auto command = parse_command_line({ "draxul", "tab", "create", "--bogus" });
        REQUIRE(command.needs_console);
        CliCapture capture;
        CHECK(dispatch_cli(command, capture.context) == 2);
        CHECK_FALSE(CliCapture::read(capture.error.get()).empty());
        CHECK(CliCapture::read(capture.output.get()).empty());
    }
    SECTION("help and launch are distinct dispatch outcomes")
    {
        CliCapture capture;
        CHECK(dispatch_cli(parse_command_line({ "draxul", "--help" }), capture.context) == 0);
        CHECK(CliCapture::read(capture.output.get()).find("tab create") != std::string::npos);
        const auto launch = parse_command_line({ "draxul", "--host", "nvim" });
        CHECK_FALSE(launch.needs_console);
        CHECK_FALSE(dispatch_cli(launch, capture.context));
    }
    SECTION("send and prompt preserve text and submit in one request")
    {
        tests::ScopedEnvVar inherited_session("DRAXUL_SESSION_ID", "inherited");
        const std::string text = "say \"hello\" 雪\nsecond line";
        for (const std::string verb : { "send", "prompt" })
        {
            INFO(verb);
            const auto command = parse_command_line({ "draxul", "agent", verb, "happy-otter", "--text", text, "--session", "explicit", "--json" });
            REQUIRE_FALSE(command.error);
            CliCapture capture;
            int requests = 0;
            capture.context.request = [&](std::string_view, const std::filesystem::path&, std::string_view method, nlohmann::json params) {
                ++requests;
                CHECK(method == "agent.send_text");
                CHECK(params.at("instance_id") == "happy-otter");
                CHECK(params.at("text") == (verb == "prompt" ? text + '\r' : text));
                CHECK(params.contains("request_id"));
                return ControlClientResult{ true, { { "accepted", true } }, {}, {} };
            };
            CHECK(dispatch_cli(command, capture.context) == 0);
            CHECK(requests == 1);
            CHECK(nlohmann::json::parse(CliCapture::read(capture.output.get())).at("accepted") == true);
        }
    }
    SECTION("prompt includes Enter in the input limit and rejects empty input")
    {
        CHECK(parse_control_cli({ "draxul", "agent", "prompt", "happy-otter", "--text", "" }).error);
        CHECK(parse_control_cli({ "draxul", "agent", "prompt", "happy-otter" }).error);
        const std::string maximum_prompt(64 * 1024 - 1, 'x');
        const auto command = parse_command_line({ "draxul", "agent", "prompt", "happy-otter", "--text", maximum_prompt, "--json" });
        REQUIRE_FALSE(command.error);
        CliCapture capture;
        capture.context.request = [&](std::string_view, const std::filesystem::path&, std::string_view method, nlohmann::json params) {
            CHECK(method == "agent.send_text");
            const auto bytes = params.at("text").get<std::string>();
            CHECK(bytes.size() == 64 * 1024);
            CHECK(bytes.back() == '\r');
            return ControlClientResult{ true, { { "accepted", true } }, {}, {} };
        };
        CHECK(dispatch_cli(command, capture.context) == 0);
        const std::string maximum_send(64 * 1024, 'x');
        CHECK(parse_control_cli({ "draxul", "agent", "prompt", "happy-otter", "--text", maximum_send }).error);
        CHECK(parse_control_cli({ "draxul", "agent", "send", "happy-otter", "--text", maximum_send }).command);
        CHECK(parse_control_cli({ "draxul", "agent", "send", "happy-otter", "--text", maximum_send + "x" }).error);
    }
}

TEST_CASE("CLI launch policy preserves platform paths and unavailable-server diagnostics", "[cli][launch]")
{
    const auto client = std::filesystem::path("/tmp/quoted app/Draxul.app/Contents/MacOS/draxul");
    const auto helper = macos_server_helper_executable(client);
    CHECK(helper == "/tmp/quoted app/Draxul.app/Contents/Helpers/Draxul Server.app/Contents/MacOS/draxul-server");
    CHECK(macos_client_executable(helper) == client);
    CHECK(macos_server_helper_executable(helper) == helper);
    CHECK(macos_server_helper_executable("/tmp/draxul") == "/tmp/draxul");
#ifdef _WIN32
    const auto windows_client = std::filesystem::path(L"C:\\quoted app\\\u96EA\\draxul.exe");
    CHECK(windows_client_executable(windows_server_helper_executable(windows_client)) == windows_client);
#endif
    ParsedArgs args;
    args.server_runtime_dir = "custom runtime";
    CHECK(cli_server_runtime_directory(args) == args.server_runtime_dir);
    args.server_status = true;
    CHECK(is_server_control_launch(args));
    CHECK_FALSE(should_use_shared_server(args));
    const auto missing = describe_server_startup_failure({}, "custom runtime");
    CHECK(missing.find("No running server was found") != std::string::npos);
    CHECK(missing.find(default_server_log_path("custom runtime").string()) != std::string::npos);
    const auto busy = describe_server_startup_failure({ .state = ServerProbeState::Busy }, "custom runtime");
    CHECK(busy.find("left running") != std::string::npos);
}

#ifdef _WIN32
TEST_CASE("Windows server helper leaves the client executable independent",
    "[server][status-surface][lifecycle]")
{
    const std::filesystem::path client
        = "D:/build/Release/draxul.exe";
    const auto helper
        = draxul::windows_server_helper_executable(client);
    CHECK(helper
        == "D:/build/Release/draxul-server.exe");
    CHECK(draxul::windows_server_helper_executable(helper)
        == helper);
    CHECK(draxul::windows_client_executable(helper)
        == client);
    CHECK(draxul::windows_client_executable(client)
        == client);
}
#endif

using namespace draxul;

TEST_CASE("server status surface formats bounded operational counts",
    "[server][status-surface][slice9]")
{
    ServerStatusSnapshot status{
        .state = "ready",
        .connected_clients = 2,
        .sessions = 2,
        .spaces = 4,
        .terminals = 5,
        .agents = 1,
        .session_statuses = {
            {
                .session_id = "default",
                .terminals = 3,
                .live_terminals = 2,
            },
            {
                .session_id = "work",
                .terminals = 2,
                .live_terminals = 1,
            },
        },
    };

    const auto text = format_server_status_text(status);
    CHECK(text.state == "Draxul Server - ready");
    CHECK(text.clients_and_terminals
        == "2 clients - 3 live terminals / 5 terminals");
    CHECK(text.sessions_spaces_and_agents
        == "2 Sessions - 4 Spaces - 1 Agent");
    CHECK(format_server_status_summary(status)
        == "Draxul Server - ready; 2 clients - 3 live terminals / 5 terminals; "
           "2 Sessions - 4 Spaces - 1 Agent");
}

TEST_CASE("server status surface keeps its log inside the runtime",
    "[server][status-surface][slice9]")
{
    const std::filesystem::path runtime
        = "D:/state/runtime/server-v1";
    CHECK(default_server_log_path(runtime)
        == runtime / "draxul-server.log");
}

TEST_CASE("server status surface formats live Session rows",
    "[server][status-surface][slice9]")
{
    const std::vector<ServerSessionStatusSnapshot> sessions{
        {
            .session_id = "default",
            .session_name = "Daily",
            .spaces = 2,
            .terminals = 3,
            .live_terminals = 2,
            .checkpoint_state = "saved",
        },
        {
            .session_id = "long-work-session",
            .session_name = "Long Work",
            .spaces = 1,
            .terminals = 12,
            .live_terminals = 0,
            .checkpoint_state = "restored",
        },
    };

    const std::string table
        = format_server_session_listing_table(sessions);
    const std::string expected
        = "SESSION ID         NAME       SPACES  TERMINALS  LIVE  CHECKPOINT\n"
          "-----------------  ---------  ------  ---------  ----  ----------\n"
          "default            Daily           2          3     2  saved\n"
          "long-work-session  Long Work       1         12     0  restored\n";
    CHECK(table == expected);
}

#ifdef __APPLE__
TEST_CASE("macOS server helper has a distinct nested app identity",
    "[server][status-surface][lifecycle]")
{
    const std::filesystem::path client
        = "/tmp/draxul.app/Contents/MacOS/draxul";
    const auto helper = macos_server_helper_executable(client);
    CHECK(helper
        == "/tmp/draxul.app/Contents/Helpers/Draxul Server.app/Contents/MacOS/draxul-server");
    CHECK(macos_server_helper_executable(helper) == helper);
    CHECK(macos_client_executable(helper) == client);
    CHECK(macos_client_executable(client) == client);
}
#endif

TEST_CASE("personal CLI requires explicit command files and keeps result identities", "[control][cli][personal]")
{
    const auto snapshot=parse_control_cli({"draxul","personal","snapshot","--json"});
    REQUIRE(snapshot.command);
    CHECK(snapshot.command->method=="personal.snapshot");
    const auto submit=parse_control_cli({"draxul","personal","submit","--file","command.json","--json"});
    REQUIRE(submit.command);
    CHECK(submit.command->method=="personal.command");
    CHECK(submit.command->reference_value=="command.json");
    CHECK(parse_control_cli({"draxul","personal","submit"}).error.has_value());
    const auto result=parse_control_cli({"draxul","personal","result","request-one"});
    REQUIRE(result.command);
    CHECK(result.command->value=="request-one");
}

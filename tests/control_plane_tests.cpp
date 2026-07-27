#include <catch2/catch_all.hpp>

#include "agent_controller.h"
#include "agent_integration.h"
#include "app.h"
#include "control_cli.h"
#include "control_event_journal.h"
#include "control_request_router.h"
#include "space_controller.h"
#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <draxul/control_plane.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <optional>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::filesystem::path unique_runtime_directory()
{
    // sockaddr_un::sun_path is 104 bytes on macOS, and the per-user $TMPDIR
    // (/var/folders/<..>/T/) already spends ~49 of them. A timestamped
    // directory under it pushed the endpoint past the limit, so bind() was
    // refused and every transport test failed before it started. Keep the base
    // shallow and the unique part short; Windows named pipes have no such
    // limit and keep using the standard temp directory.
#ifdef _WIN32
    const std::filesystem::path base = std::filesystem::temp_directory_path();
#else
    const std::filesystem::path base = "/tmp";
#endif
    // pid + a process-local counter: short enough for sun_path and genuinely
    // unique. (A truncated nanosecond clock is not — modulo 1e6 wraps every
    // millisecond, so two tests starting back to back share a directory and
    // one clobbers the other's endpoint metadata.)
    static std::atomic<unsigned> counter{ 0 };
#ifdef _WIN32
    const auto pid = static_cast<long long>(_getpid());
#else
    const auto pid = static_cast<long long>(::getpid());
#endif
    return base / ("dxl-ctl-" + std::to_string(pid) + "-" + std::to_string(counter.fetch_add(1)));
}

std::string test_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT)
        + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

class ScopedEnvironment
{
public:
    ScopedEnvironment(std::string name, std::string value)
        : name_(std::move(name))
    {
        if (const char* existing = std::getenv(name_.c_str()))
            previous_ = existing;
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvironment()
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), previous_.value_or("").c_str());
#else
        if (previous_)
            setenv(name_.c_str(), previous_->c_str(), 1);
        else
            unsetenv(name_.c_str());
#endif
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

ControlClientResult request_while_pumping(App& app,
    std::string_view session_id,
    const std::filesystem::path& runtime,
    std::string_view method,
    nlohmann::json params)
{
    auto future = std::async(std::launch::async,
        [=] { return ControlClient::request(session_id, runtime, method, params); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (future.wait_for(std::chrono::milliseconds(0))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        app.run_smoke_test(std::chrono::milliseconds(5));
    }
    if (future.wait_for(std::chrono::milliseconds(0))
        != std::future_status::ready)
        return { false, nullptr, "timeout", "Test control request timed out." };
    return future.get();
}

} // namespace

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
}

TEST_CASE("Codex integration install is idempotent and preserves unrelated hooks",
    "[control][integration]")
{
    TempDir codex("draxul-codex-integration");
    const auto hooks_path = codex.path / "hooks.json";
    const auto config_path = codex.path / "config.toml";
    {
        std::ofstream hooks(hooks_path);
        hooks << R"({"hooks":{"Stop":[{"hooks":[{"type":"command","command":"keep-me"}]}]}})";
        std::ofstream config(config_path);
        config << "model = \"gpt-5\"\n";
    }
    ScopedEnvironment codex_home("CODEX_HOME", codex.path.string());

    IntegrationCliCommand install{ .action = "install", .target = "codex" };
    REQUIRE(run_integration_cli(install) == 0);
    REQUIRE(run_integration_cli(install) == 0);

    {
        std::ifstream hooks_input(hooks_path);
        const auto hooks = nlohmann::json::parse(hooks_input);
        CHECK(hooks["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
        REQUIRE(hooks["hooks"]["SessionStart"].size() == 1);
        CHECK(hooks["hooks"]["SessionStart"][0]["hooks"][0]["command"]
                  .get<std::string>()
                  .find("draxul-agent-session")
            != std::string::npos);
        std::ifstream config_input(config_path);
        const std::string config((std::istreambuf_iterator<char>(config_input)),
            std::istreambuf_iterator<char>());
        CHECK(config.find("model = \"gpt-5\"") != std::string::npos);
        CHECK(config.find("hooks = true") != std::string::npos);
    }

    IntegrationCliCommand uninstall{ .action = "uninstall", .target = "codex" };
    REQUIRE(run_integration_cli(uninstall) == 0);
    std::ifstream remaining_input(hooks_path);
    const auto remaining = nlohmann::json::parse(remaining_input);
    CHECK(remaining["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
    CHECK(remaining["hooks"]["SessionStart"].empty());
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

TEST_CASE("Claude integration install is idempotent and preserves unrelated settings",
    "[control][integration]")
{
    TempDir claude("draxul-claude-integration");
    const auto settings_path = claude.path / "settings.json";
    {
        std::ofstream settings(settings_path);
        settings << R"({
  "permissions": {"allow": ["Read"]},
  "hooks": {
    "Stop": [{"hooks": [{"type": "command", "command": "keep-me"}]}]
  }
})";
    }
    ScopedEnvironment claude_home("CLAUDE_CONFIG_DIR", claude.path.string());

    IntegrationCliCommand install{ .action = "install", .target = "claude" };
    REQUIRE(run_integration_cli(install) == 0);
    REQUIRE(run_integration_cli(install) == 0);

#ifdef _WIN32
    const auto hook_path = claude.path / "hooks" / "draxul-agent-session.ps1";
#else
    const auto hook_path = claude.path / "hooks" / "draxul-agent-session.sh";
#endif
    REQUIRE(std::filesystem::exists(hook_path));
    {
        std::ifstream hook_input(hook_path);
        const std::string hook((std::istreambuf_iterator<char>(hook_input)),
            std::istreambuf_iterator<char>());
        CHECK(hook.find("DRAXUL_INTEGRATION_ID=claude") != std::string::npos);
        CHECK(hook.find("draxul:claude") != std::string::npos);
        CHECK(hook.find("agent_id") != std::string::npos);

        std::ifstream settings_input(settings_path);
        const auto settings = nlohmann::json::parse(settings_input);
        CHECK(settings["permissions"]["allow"][0] == "Read");
        CHECK(settings["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
        REQUIRE(settings["hooks"]["SessionStart"].size() == 1);
        const auto& group = settings["hooks"]["SessionStart"][0];
        CHECK(group["matcher"] == "*");
        CHECK(group["hooks"][0]["command"]
                  .get<std::string>()
                  .find("draxul-agent-session")
            != std::string::npos);
    }

    IntegrationCliCommand uninstall{ .action = "uninstall", .target = "claude" };
    REQUIRE(run_integration_cli(uninstall) == 0);
    CHECK_FALSE(std::filesystem::exists(hook_path));
    std::ifstream remaining_input(settings_path);
    const auto remaining = nlohmann::json::parse(remaining_input);
    CHECK(remaining["permissions"]["allow"][0] == "Read");
    CHECK(remaining["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
    CHECK(remaining["hooks"]["SessionStart"].empty());
}

TEST_CASE("control event subscriptions are bounded cursor projections", "[control]")
{
    ControlEventJournal journal;
    journal.record("space.focused", { { "space_id", 2 } });
    journal.record("agent.changed", { { "instance_id", "agent-1" } });

    const auto first = journal.read_after(0, 1);
    REQUIRE(first["events"].size() == 1);
    CHECK(first["events"][0]["type"] == "space.focused");
    const uint64_t cursor = first["next_cursor"].get<uint64_t>();

    const auto second = journal.read_after(cursor, 4);
    REQUIRE(second["events"].size() == 1);
    CHECK(second["events"][0]["type"] == "agent.changed");
    CHECK_FALSE(second["cursor_expired"].get<bool>());
}

TEST_CASE("control router projects Spaces without exposing mutable state", "[control]")
{
    SpaceController spaces("D:/work/project");
    AgentController agents;
    ControlRequestRouter router(spaces, agents, "test-session");

    auto hello = router.handle({ "1", "system.hello", {} });
    REQUIRE(hello.ok);
    CHECK(hello.value["session_id"] == "test-session");
    CHECK(hello.value["protocol_version"] == kControlProtocolVersion);

    auto list = router.handle({ "2", "space.list", {} });
    REQUIRE(list.ok);
    REQUIRE(list.value.size() == 1);
    CHECK(list.value[0]["id"] == kDefaultSpaceId);
    CHECK(list.value[0]["active"] == true);

    auto missing = router.handle({ "3", "space.get", { { "id", 99 } } });
    CHECK_FALSE(missing.ok);
    CHECK(missing.error_code == "not_found");
}

TEST_CASE("a second server refuses a live endpoint and leaves the incumbent intact",
    "[control]")
{
    // Sessions are single-owner. The second server must report the endpoint as
    // taken rather than unlinking the live socket and overwriting the token,
    // which silently rerouted every CLI request to the newcomer.
    const auto runtime = unique_runtime_directory();
    ControlServer first;
    std::string first_error;
    REQUIRE(first.start("dup-session", runtime, [] {}, &first_error));
    REQUIRE(first.running());
    const auto metadata = first.metadata_path();
    REQUIRE(std::filesystem::exists(metadata));

    std::ifstream metadata_input(metadata);
    const auto original = nlohmann::json::parse(metadata_input);

    ControlServer second;
    std::string second_error;
    CHECK_FALSE(second.start("dup-session", runtime, [] {}, &second_error));
    CHECK(second.endpoint_in_use());
    CHECK_FALSE(second.running());
    CHECK_FALSE(second_error.empty());

    // The incumbent still owns its endpoint, and its credentials are unchanged.
    CHECK(first.running());
    REQUIRE(std::filesystem::exists(metadata));
    std::ifstream after_input(metadata);
    const auto after = nlohmann::json::parse(after_input);
    CHECK(after.at("token") == original.at("token"));
    metadata_input.close();
    after_input.close();
#ifndef _WIN32
    CHECK(std::filesystem::exists(first.endpoint()));
#endif

    first.stop();
    CHECK_FALSE(std::filesystem::exists(metadata));
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

#ifndef _WIN32
TEST_CASE("a stale endpoint from a dead owner is reclaimed", "[control]")
{
    // The flip side of refusing a live endpoint: one left behind by a process
    // that crashed must not lock the Session out forever. (POSIX only —
    // Windows named pipes leave no filesystem residue to go stale.)
    const auto runtime = unique_runtime_directory();
    std::string endpoint_path;
    {
        ControlServer previous;
        std::string error;
        REQUIRE(previous.start("stale-session", runtime, [] {}, &error));
        endpoint_path = previous.endpoint();
        previous.stop();
    }
    // Nothing is listening, but the path exists — exactly a crashed owner.
    {
        std::ofstream stale(endpoint_path);
        stale << "stale";
    }
    REQUIRE(std::filesystem::exists(endpoint_path));

    ControlServer fresh;
    std::string error;
    CHECK(fresh.start("stale-session", runtime, [] {}, &error));
    CHECK(fresh.running());
    CHECK_FALSE(fresh.endpoint_in_use());
    fresh.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}
#endif

TEST_CASE("control transport authenticates and dispatches on the caller thread", "[control]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start("transport-test", runtime, [] {}, &start_error));
    REQUIRE(std::filesystem::exists(server.metadata_path()));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request(
            "transport-test", runtime, "system.hello", { { "probe", true } });
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");
    CHECK(response.result["probe"] == true);

    const auto metadata = server.metadata_path();
    server.stop();
    CHECK_FALSE(std::filesystem::exists(metadata));
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("app control endpoint starts, prompts, waits, and emits events", "[control][app]")
{
    TempDir home("dxl-ctl");
    HomeDirRedirect redirect(home.path);
    std::vector<FakeHost*> hosts;

    AppOptions options;
    options.load_user_config = false;
    options.save_user_config = false;
    options.activate_window_on_startup = false;
    options.clamp_window_to_display = false;
    options.override_display_ppi = 96.0f;
    options.config_overrides.font_path = test_font_path();
    options.window_factory = [] { return std::make_unique<FakeWindow>(); };
    options.renderer_create_fn = [](int, RendererOptions) {
        return RendererBundle{ std::make_unique<FakeTermRenderer>() };
    };
    options.host_factory = [&](HostKind) {
        auto host = std::make_unique<FakeHost>("control-host");
        hosts.push_back(host.get());
        return host;
    };
    options.session_id = "ctl-app";
    options.enable_control_server = true;

    App app(std::move(options));
    REQUIRE(app.initialize());
    const auto runtime = control_runtime_directory(
        ConfigDocument::default_path().parent_path());

    auto started = request_while_pumping(app, "ctl-app", runtime,
        "agent.start", { { "profile_id", "codex" }, { "args", nlohmann::json::array() } });
    REQUIRE(started.ok);
    const std::string instance_id = started.result.value("instance_id", "");
    const std::string pane_id = started.result["route"].value("pane_id", "");
    REQUIRE_FALSE(instance_id.empty());
    REQUIRE(hosts.size() == 2);

    auto reported = request_while_pumping(app, "ctl-app", runtime,
        "pane.report_agent_session",
        { { "pane_id", pane_id }, { "agent_instance_id", instance_id },
            { "source", "draxul:codex" }, { "agent", "codex" },
            { "integration_version", 1 }, { "sequence", 1 },
            { "ref_kind", "id" }, { "ref_value", "codex-session-1" } });
    REQUIRE(reported.ok);
    CHECK(reported.result["native_session"]["source"] == "draxul:codex");

    auto sent = request_while_pumping(app, "ctl-app", runtime,
        "agent.send_text",
        { { "instance_id", instance_id }, { "text", "Review this" } });
    REQUIRE(sent.ok);
    REQUIRE(hosts.back()->sent_agent_input.size() == 1);
    CHECK(hosts.back()->sent_agent_input.back() == "Review this");

    auto waiting = request_while_pumping(app, "ctl-app", runtime,
        "agent.wait",
        { { "instance_id", instance_id }, { "until", { "done" } } });
    REQUIRE(waiting.ok);
    CHECK_FALSE(waiting.result.value("complete", true));
    CHECK(waiting.result["agent"]["runtime_generation"].get<uint64_t>() > 0);

    auto events = request_while_pumping(
        app, "ctl-app", runtime, "event.subscribe",
        nlohmann::json::object());
    INFO(events.error_code << ": " << events.error_message);
    REQUIRE(events.ok);
    CHECK_FALSE(events.result["events"].empty());

    app.shutdown();
    CHECK_FALSE(std::filesystem::exists(
        control_metadata_path(runtime, "ctl-app")));
}

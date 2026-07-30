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
#include <draxul/remote_session_client.h>
#include <draxul/server_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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

    auto report = parse_control_cli({
        "draxul", "pane", "report-agent-session", "pane-7",
        "--agent-instance", "agent-7",
        "--source", "draxul:codex",
        "--agent", "codex",
        "--integration-version", "1",
        "--sequence", "9",
        "--session-ref", "native-7",
        "--server-epoch", "epoch-7",
        "--runtime-generation", "3",
        "--server-runtime-dir", "D:/runtime",
    });
    REQUIRE(report.command);
    CHECK(report.command->server_epoch == "epoch-7");
    CHECK(report.command->runtime_generation == 3);
    CHECK(report.command->server_runtime_directory
        == "D:/runtime");
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
#ifdef _WIN32
        const auto hook_path
            = codex.path / "draxul-agent-session.ps1";
#else
        const auto hook_path
            = codex.path / "draxul-agent-session.sh";
#endif
        std::ifstream hook_input(hook_path);
        const std::string hook(
            (std::istreambuf_iterator<char>(hook_input)),
            std::istreambuf_iterator<char>());
        CHECK(hook.find("DRAXUL_INTEGRATION_VERSION=2")
            != std::string::npos);
        CHECK(hook.find("DRAXUL_SERVER_EPOCH")
            != std::string::npos);
        CHECK(hook.find("runtime-generation")
            != std::string::npos);

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
        CHECK(hook.find("DRAXUL_INTEGRATION_VERSION=2")
            != std::string::npos);
        CHECK(hook.find("draxul:claude") != std::string::npos);
        CHECK(hook.find("agent_id") != std::string::npos);
        CHECK(hook.find("DRAXUL_SERVER_EPOCH")
            != std::string::npos);
        CHECK(hook.find("runtime-generation")
            != std::string::npos);

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

TEST_CASE("control requests obey one absolute deadline",
    "[control][deadline]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::atomic<bool> request_queued = false;
    std::string start_error;
    REQUIRE(server.start("deadline-test", runtime,
        [&] { request_queued = true; }, &start_error));

    const auto started_at = std::chrono::steady_clock::now();
    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("deadline-test", runtime,
            "test.stalled", nlohmann::json::object(),
            { .timeout = std::chrono::milliseconds(75) });
    });
    const auto queue_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(1);
    while (!request_queued
        && std::chrono::steady_clock::now() < queue_deadline)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }
    REQUIRE(request_queued);
    REQUIRE(client.wait_for(std::chrono::milliseconds(500))
        == std::future_status::ready);
    const auto response = client.get();
    CHECK_FALSE(response.ok);
    CHECK(response.error_code == "deadline_exceeded");
    CHECK(std::chrono::steady_clock::now() - started_at
        < std::chrono::milliseconds(500));

    // The caller's remaining budget travels with the request. Once it expires,
    // process_pending must discard the cancelled work instead of applying it
    // after the caller has given up.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::atomic<int> handler_calls = 0;
    server.process_pending([&](const ControlRequest&) {
        ++handler_calls;
        return ControlMethodResult::success(true);
    });
    CHECK(handler_calls == 0);

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control clients reuse recently read endpoint metadata",
    "[control][metadata]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        "metadata-cache-test", runtime, [] {}, &start_error));

    auto request = [&] {
        auto client = std::async(std::launch::async, [&] {
            return ControlClient::request("metadata-cache-test",
                runtime, "test.cached");
        });
        const auto deadline
            = std::chrono::steady_clock::now()
            + std::chrono::seconds(2);
        while (client.wait_for(std::chrono::milliseconds(0))
                != std::future_status::ready
            && std::chrono::steady_clock::now() < deadline)
        {
            server.process_pending([](const ControlRequest&) {
                return ControlMethodResult::success(true);
            });
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
        REQUIRE(client.wait_for(std::chrono::milliseconds(0))
            == std::future_status::ready);
        return client.get();
    };

    REQUIRE(request().ok);
    std::error_code remove_error;
    REQUIRE(std::filesystem::remove(
        server.metadata_path(), remove_error));
    REQUIRE_FALSE(remove_error);
    CHECK(request().ok);

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("remote Session client publishes topology and command results",
    "[control][client-worker]")
{
    const auto runtime = unique_runtime_directory();
    const std::string control_id
        = namespaced_control_id(kServerControlId, runtime);
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(control_id, runtime, [] {}, &start_error));

    TopologySnapshot topology{
        .revision = 1,
        .session_id = "default",
        .spaces = {
            {
                .space_id = "space-1",
                .name = "Work",
                .tabs = {
                    {
                        .tab_id = "tab-1",
                        .name = "Tab",
                        .root_node_id = "node-1",
                        .nodes = {
                            {
                                .node_id = "node-1",
                                .is_leaf = true,
                                .pane_id = "pane-1",
                            },
                        },
                        .panes = {
                            {
                                .pane_id = "pane-1",
                                .name = "Shell",
                                .domain
                                = TopologyPaneDomain::ClientLocal,
                                .client_host_kind
                                = "platform_default",
                            },
                        },
                    },
                },
            },
        },
    };
    ServerAgentSnapshot agents{
        .revision = 1,
        .session_id = "default",
    };
    std::atomic<int> command_count = 0;
    std::vector<std::chrono::steady_clock::time_point>
        command_attempts;
    auto dispatch = [&](const ControlRequest& request) {
        if (request.method == "topology.snapshot")
            return ControlMethodResult::success(
                topology_snapshot_to_json(topology));
        if (request.method == "agent.snapshot")
            return ControlMethodResult::success(
                server_agent_snapshot_to_json(agents));
        if (request.method == "server.status")
        {
            return ControlMethodResult::success(
                server_status_to_json({
                    .state = "ready",
                    .protocol_major = kServerProtocolMajor,
                    .protocol_minor = kServerProtocolMinor,
                    .server_pid = 1,
                    .server_epoch = "warning-epoch",
                    .build_version = "test",
                    .sessions = 1,
                    .checkpoint_state = "recovered",
                    .restore_warnings
                    = { "Imported one legacy Session." },
                    .session_statuses = {
                        {
                            .session_id = "default",
                            .session_name = "default",
                            .checkpoint_state
                            = "restored_with_warnings",
                            .restore_warnings
                            = { "One pane was skipped." },
                        },
                    },
                }));
        }
        if (request.method == "topology.poll")
        {
            return ControlMethodResult::success({
                { "changed", false },
                { "revision", topology.revision },
            });
        }
        if (request.method == "agent.poll")
        {
            return ControlMethodResult::success({
                { "changed", false },
                { "revision", agents.revision },
            });
        }
        if (request.method == "topology.command")
        {
            const int attempt = ++command_count;
            command_attempts.push_back(
                std::chrono::steady_clock::now());
            if (attempt == 1)
            {
                return ControlMethodResult::error(
                    "io_error",
                    "Synthetic topology transport interruption.");
            }
            topology.spaces.front().name
                = request.params.value("name", "Renamed");
            ++topology.revision;
            return ControlMethodResult::success(
                topology_command_result_to_json({
                    .applied = true,
                    .snapshot = topology,
                }));
        }
        return ControlMethodResult::error(
            "unknown_method", "Unexpected test method.");
    };

    RemoteSessionClient client({
        .runtime_directory = runtime,
        .client_id = "ui-test",
        .session_id = "default",
    });
    REQUIRE(client.start());
    bool saw_topology = false;
    bool saw_agents = false;
    bool saw_persistence_warning = false;
    const auto initial_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while ((!saw_topology || !saw_agents
               || !saw_persistence_warning)
        && std::chrono::steady_clock::now() < initial_deadline)
    {
        server.process_pending(dispatch);
        if (auto state = client.take_published_state())
        {
            saw_topology
                = saw_topology || state->topology.has_value();
            saw_agents = saw_agents || state->agents.has_value();
            saw_persistence_warning
                = saw_persistence_warning
                || !state->persistence_warnings.empty();
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }
    REQUIRE(saw_topology);
    REQUIRE(saw_agents);
    REQUIRE(saw_persistence_warning);

    REQUIRE(client.enqueue({
        .command_id = "rename-1",
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = "space-1",
        .name = "Renamed",
    }));
    bool completed = false;
    const auto command_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (!completed
        && std::chrono::steady_clock::now() < command_deadline)
    {
        server.process_pending(dispatch);
        if (auto state = client.take_published_state())
        {
            for (const auto& completion : state->commands)
            {
                completed = completion.ok
                    && completion.snapshot
                    && completion.snapshot->revision == 2;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }
    CHECK(completed);
    CHECK(command_count == 2);
    REQUIRE(command_attempts.size() == 2);
    CHECK(command_attempts[1] - command_attempts[0]
        >= std::chrono::milliseconds(75));

    client.stop();
    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("remote Session client republishes snapshots until epoch-aware acknowledgement",
    "[control][client-worker][topology][ack]")
{
    const auto runtime = unique_runtime_directory();
    const std::string control_id
        = namespaced_control_id(kServerControlId, runtime);
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        control_id, runtime, [] {}, &start_error));

    TopologySnapshot topology{
        .revision = 1,
        .session_id = "default",
        .spaces = {
            {
                .space_id = "space-1",
                .name = "Work",
                .tabs = {
                    {
                        .tab_id = "tab-1",
                        .name = "Tab",
                        .root_node_id = "node-1",
                        .nodes = {
                            {
                                .node_id = "node-1",
                                .is_leaf = true,
                                .pane_id = "pane-1",
                            },
                        },
                        .panes = {
                            {
                                .pane_id = "pane-1",
                                .domain
                                = TopologyPaneDomain::ClientLocal,
                                .client_host_kind
                                = "platform_default",
                            },
                        },
                    },
                },
            },
        },
    };
    bool force_changed = false;
    auto dispatch = [&](const ControlRequest& request) {
        if (request.method == "topology.snapshot")
        {
            return ControlMethodResult::success(
                topology_snapshot_to_json(topology));
        }
        if (request.method == "topology.poll")
        {
            const uint64_t after
                = request.params.value("after_revision", 0ULL);
            const bool changed
                = force_changed || after < topology.revision;
            force_changed = false;
            nlohmann::json result{
                { "changed", changed },
                { "revision", topology.revision },
            };
            if (changed)
            {
                result["snapshot"]
                    = topology_snapshot_to_json(topology);
            }
            return ControlMethodResult::success(
                std::move(result));
        }
        return ControlMethodResult::error(
            "unknown_method", "Not used by this test.");
    };

    auto recovery
        = std::make_shared<ClientRecoveryState>("ack-test");
    REQUIRE(recovery->set_server_epoch("epoch-a"));
    RemoteSessionClient client({
        .runtime_directory = runtime,
        .client_id = "ack-ui",
        .session_id = "default",
        .recovery = recovery,
    });
    REQUIRE(client.start());

    const auto take_revision = [&](uint64_t revision,
                                   std::string_view epoch,
                                   std::chrono::milliseconds timeout) {
        const auto deadline
            = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            server.process_pending(dispatch);
            if (auto state = client.take_published_state();
                state && state->topology
                && state->topology->revision == revision
                && state->topology_server_epoch == epoch)
            {
                return true;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
        return false;
    };

    REQUIRE(take_revision(
        1, "epoch-a", std::chrono::seconds(2)));
    // Without an acknowledgement, an unchanged server poll republishes the
    // pending snapshot so a transient UI projection failure can retry.
    REQUIRE(take_revision(
        1, "epoch-a", std::chrono::seconds(1)));
    client.acknowledge_topology("epoch-a", 1);

    bool saw_acked_revision = false;
    const auto quiet_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < quiet_deadline)
    {
        server.process_pending(dispatch);
        if (auto state = client.take_published_state();
            state && state->topology
            && state->topology->revision == 1)
        {
            saw_acked_revision = true;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }
    CHECK_FALSE(saw_acked_revision);

    topology.revision = 2;
    REQUIRE(take_revision(
        2, "epoch-a", std::chrono::seconds(1)));
    topology.revision = 3;
    // The newest server snapshot replaces an older unacknowledged one.
    REQUIRE(take_revision(
        3, "epoch-a", std::chrono::seconds(1)));

    REQUIRE(recovery->set_server_epoch("epoch-b"));
    force_changed = true;
    REQUIRE(take_revision(
        3, "epoch-b", std::chrono::seconds(1)));
    // A late ack from the old server cannot consume a same-revision snapshot
    // published by its replacement.
    client.acknowledge_topology("epoch-a", 3);
    REQUIRE(take_revision(
        3, "epoch-b", std::chrono::seconds(1)));
    client.acknowledge_topology("epoch-b", 3);

    client.stop();
    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control server stop fails queued dispatch before listener join",
    "[control][shutdown]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::atomic<bool> request_queued = false;
    std::string start_error;
    REQUIRE(server.start("stop-pending-test", runtime,
        [&] { request_queued = true; }, &start_error));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("stop-pending-test", runtime,
            "test.never_processed", nlohmann::json::object());
    });
    const auto queue_deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!request_queued
        && std::chrono::steady_clock::now() < queue_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request_queued);

    const auto started_at = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    CHECK(elapsed < std::chrono::seconds(2));
    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    CHECK_FALSE(response.ok);
    CHECK(response.error_code == "server_stopping");

    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control transport replaces invalid UTF-8 in response payloads",
    "[control][unicode]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        "invalid-utf8-response", runtime, [] {}, &start_error));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request(
            "invalid-utf8-response", runtime, "test.invalid_utf8",
            nlohmann::json::object());
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest&) {
            return ControlMethodResult::success({
                { "text", std::string(1, static_cast<char>(0xFF)) },
            });
        });
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    INFO(response.error_code << ": " << response.error_message);
    REQUIRE(response.ok);
    CHECK(response.result["text"] == "\xEF\xBF\xBD");

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

#ifdef _WIN32
TEST_CASE("a stalled Windows control client does not starve another client",
    "[control][windows]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start("concurrent-transport-test", runtime, [] {},
        &start_error));

    const std::string endpoint = server.endpoint();
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    HANDLE stalled = CreateFileW(pipe_name.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    REQUIRE(stalled != INVALID_HANDLE_VALUE);

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("concurrent-transport-test", runtime,
            "system.hello", { { "probe", true } });
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
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
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");
    CloseHandle(stalled);
    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}
#endif

#ifndef _WIN32
TEST_CASE("concurrent launchers serialize stale endpoint recovery", "[control]")
{
    const auto runtime = unique_runtime_directory();
    std::string endpoint_path;
    {
        ControlServer previous;
        std::string error;
        REQUIRE(previous.start("racing-stale-session", runtime, [] {}, &error));
        endpoint_path = previous.endpoint();
        previous.stop();
    }
    {
        std::ofstream stale(endpoint_path);
        stale << "stale";
    }
    REQUIRE(std::filesystem::exists(endpoint_path));

    ControlServer first;
    ControlServer second;
    std::string first_error;
    std::string second_error;
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    int ready = 0;
    bool launch = false;
    auto start = [&](ControlServer& server, std::string& error) {
        {
            std::unique_lock lock(gate_mutex);
            ++ready;
            gate_changed.notify_all();
            gate_changed.wait(lock, [&] { return launch; });
        }
        return server.start(
            "racing-stale-session", runtime, [] {}, &error);
    };

    auto first_result = std::async(
        std::launch::async, [&] { return start(first, first_error); });
    auto second_result = std::async(
        std::launch::async, [&] { return start(second, second_error); });
    {
        std::unique_lock lock(gate_mutex);
        gate_changed.wait(lock, [&] { return ready == 2; });
        launch = true;
    }
    gate_changed.notify_all();

    const bool first_started = first_result.get();
    const bool second_started = second_result.get();
    CHECK(first_started != second_started);
    ControlServer& winner = first_started ? first : second;
    ControlServer& loser = first_started ? second : first;
    CHECK(winner.running());
    CHECK_FALSE(loser.running());
    CHECK(loser.endpoint_in_use());
    REQUIRE(std::filesystem::exists(winner.metadata_path()));
    REQUIRE(std::filesystem::exists(winner.endpoint()));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("racing-stale-session", runtime,
            "system.hello", { { "probe", true } });
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        winner.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
    }
    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");

    winner.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("symlinked runtime directories share one control endpoint", "[control]")
{
    const auto runtime = unique_runtime_directory();
    auto alias = runtime;
    alias += "-alias";
    std::error_code path_error;
    std::filesystem::create_directories(runtime, path_error);
    REQUIRE_FALSE(path_error);
    std::filesystem::create_directory_symlink(runtime, alias, path_error);
    REQUIRE_FALSE(path_error);

    CHECK(namespaced_control_id("draxul-server", runtime)
        == namespaced_control_id("draxul-server", alias));

    ControlServer first;
    std::string first_error;
    REQUIRE(first.start(
        "symlink-session", runtime, [] {}, &first_error));
    std::ifstream metadata_input(first.metadata_path());
    const auto original = nlohmann::json::parse(metadata_input);
    metadata_input.close();

    ControlServer second;
    std::string second_error;
    CHECK_FALSE(second.start(
        "symlink-session", alias, [] {}, &second_error));
    CHECK(second.endpoint_in_use());
    REQUIRE(std::filesystem::exists(first.metadata_path()));
    std::ifstream after_input(first.metadata_path());
    const auto after = nlohmann::json::parse(after_input);
    CHECK(after.at("token") == original.at("token"));

    first.stop();
    std::filesystem::remove(alias, path_error);
    std::filesystem::remove_all(runtime, path_error);
}
#endif

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

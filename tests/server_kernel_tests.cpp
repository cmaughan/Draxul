#include <catch2/catch_test_macros.hpp>

#include "../libs/draxul-server/src/fake_terminal_runtime.h"
#include "../libs/draxul-server/src/remote_terminal_service.h"
#include "server_agent_service.h"
#include "../libs/draxul-server/src/server_terminal_runtime.h"
#include "../libs/draxul-server/src/session_poll_service.h"
#include "../libs/draxul-server/src/session_topology_bridge.h"
#include "../libs/draxul-server/src/topology_service.h"
#include "support/temp_dir.h"

#include <draxul/agent_client.h>
#include <draxul/agent_protocol.h>
#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>
#include <draxul/session_state.h>
#include <draxul/topology_client.h>

#include <cstdlib>
#include <fstream>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <tlhelp32.h>
#else
#include <csignal>
#include <unistd.h>
#endif

using namespace draxul;
using draxul::tests::TempDir;

TEST_CASE("server terminal scrollback storage is lazy",
    "[server][remote-terminal][resource-bounds]")
{
    ServerTerminalRuntime runtime({
        .scrollback_capacity = 1'000'000,
    });
    CHECK_FALSE(runtime.scrollback_storage_initialized());
    CHECK(runtime.scrollback_rows() == 0);
}

TEST_CASE("Session poll keeps duplicate terminal subscriptions independent",
    "[server][session-poll][remote-terminal]")
{
    FakeTerminalRuntime runtime;
    RemoteTerminalService service(
        {
            .method_prefix = "session-test",
            .server_epoch = "session-epoch",
            .pane_id = "pane-a",
            .terminal_id = "terminal-a",
            .name = "Terminal A",
        },
        runtime);
    TopologyService topology("default");
    ServerAgentService agents("default");
    SessionPollService poll("session-epoch");
    const std::array terminal_views{
        SessionPollTerminalView{
            .terminal_id = "terminal-a",
            .service = &service,
        },
    };
    SessionPollRequest initial{
        .request_serial = 1,
        .server_epoch = "session-epoch",
        .topology_after_revision = topology.snapshot().revision,
        .agent_after_revision = agents.snapshot().revision,
        .terminals = {
            {
                .subscription_id = 1,
                .terminal_id = "terminal-a",
                .visibility_generation = 1,
            },
            {
                .subscription_id = 2,
                .terminal_id = "terminal-a",
                .visibility_generation = 1,
            },
        },
    };
    auto initial_json = session_poll_request_to_json(initial);
    const auto first = poll.handle(initial_json, "client-a",
        topology.snapshot(), agents.snapshot(), terminal_views);
    REQUIRE(first.ok);
    std::string parse_error;
    const auto first_response = session_poll_response_from_json(
        first.value, parse_error);
    INFO(parse_error);
    REQUIRE(first_response);
    REQUIRE(first_response->terminals.size() == 2);
    REQUIRE(first_response->terminals[0].attach);
    REQUIRE(first_response->terminals[1].attach);
    const auto cursor_for = [&](uint64_t subscription_id) {
        const auto found = std::ranges::find_if(
            first_response->terminals,
            [subscription_id](const auto& batch) {
                return batch.subscription_id == subscription_id;
            });
        REQUIRE(found != first_response->terminals.end());
        return SessionTerminalCursor{
            .generation = found->attach->state.version.generation,
            .after_sequence
            = found->attach->state.version.sequence,
        };
    };
    const auto first_cursor = cursor_for(1);
    const auto second_cursor = cursor_for(2);

    REQUIRE(service.handle("session-test.resize",
        {
            { "client_id", "client-a" },
            { "request_id", uint64_t{ 9 } },
            { "cols", 90 },
            { "rows", 30 },
        }).ok);
    SessionPollRequest changed = initial;
    changed.request_serial = 2;
    changed.terminals[0].cursor = first_cursor;
    changed.terminals[0].visible = false;
    changed.terminals[0].visibility_generation = 2;
    changed.terminals[1].cursor = second_cursor;
    const auto changed_result = poll.handle(
        session_poll_request_to_json(changed), "client-a",
        topology.snapshot(), agents.snapshot(), terminal_views);
    REQUIRE(changed_result.ok);
    const auto changed_response = session_poll_response_from_json(
        changed_result.value, parse_error);
    INFO(parse_error);
    REQUIRE(changed_response);
    const auto hidden = std::ranges::find_if(
        changed_response->terminals, [](const auto& batch) {
            return batch.subscription_id == 1;
        });
    const auto visible = std::ranges::find_if(
        changed_response->terminals, [](const auto& batch) {
            return batch.subscription_id == 2;
        });
    REQUIRE(hidden != changed_response->terminals.end());
    CHECK(hidden->suspended);
    REQUIRE(visible != changed_response->terminals.end());
    CHECK_FALSE(visible->events.empty());
    CHECK(session_poll_response_to_json(*changed_response).dump().size()
        <= kControlMaxMessageBytes);
}

TEST_CASE("server scrollback budget reserves rejects and releases cells",
    "[server][remote-terminal][resource-bounds]")
{
    ServerTerminalResourceBudget budget(100);
    CHECK(budget.replace_scrollback_reservation(0, 60));
    CHECK(budget.replace_scrollback_reservation(0, 30));
    CHECK(budget.reserved_scrollback_cells() == 90);
    CHECK_FALSE(
        budget.replace_scrollback_reservation(0, 11));
    CHECK(budget.reserved_scrollback_cells() == 90);

    CHECK(budget.replace_scrollback_reservation(60, 40));
    CHECK(budget.replace_scrollback_reservation(30, 60));
    CHECK(budget.reserved_scrollback_cells() == 100);
    CHECK(budget.replace_scrollback_reservation(40, 0));
    CHECK(budget.replace_scrollback_reservation(60, 0));
    CHECK(budget.reserved_scrollback_cells() == 0);
    CHECK(budget.max_scrollback_cells() == 100);
}

TEST_CASE("remote terminal subscriber queues are byte bounded",
    "[server][remote-terminal][resource-bounds]")
{
    constexpr size_t kTestQueueByteLimit = 4 * 1024;
    FakeTerminalRuntime runtime;
    RemoteTerminalService service(
        {
            .method_prefix = "bytes",
            .server_epoch = "bytes-epoch",
            .pane_id = "bytes-pane",
            .terminal_id = "bytes-terminal",
            .name = "Bytes",
            .subscriber_queue_byte_limit = kTestQueueByteLimit,
        },
        runtime);
    REQUIRE(service.handle(
                       "bytes.attach",
                       { { "client_id", "controller" } })
            .ok);
    REQUIRE(service.handle(
                       "bytes.attach",
                       { { "client_id", "observer" } })
            .ok);

    // A modest dirty grid exceeds the deliberately small test queue. This
    // exercises the production overflow/resync path without allocating and
    // serializing four maximum-size terminal snapshots.
    REQUIRE(service.handle(
                       "bytes.resize",
                       {
                           { "client_id", "controller" },
                           { "request_id", uint64_t{ 1 } },
                           { "cols", 64 },
                           { "rows", 32 },
                       })
            .ok);

    const auto metrics = service.handle(
        "bytes.metrics", nlohmann::json::object());
    REQUIRE(metrics.ok);
    CHECK(metrics.value["max_queue_bytes"].get<size_t>()
        <= kTestQueueByteLimit);
    CHECK(metrics.value["queue_byte_limit"].get<size_t>()
        == kTestQueueByteLimit);
    CHECK(metrics.value["oversized_queue_events"].get<uint64_t>() > 0);
    CHECK(metrics.value["resyncs"].get<uint64_t>() > 0);

    const auto poll = service.handle(
        "bytes.poll",
        {
            { "client_id", "observer" },
            { "server_epoch", "bytes-epoch" },
            { "terminal_id", "bytes-terminal" },
            { "generation", uint64_t{ 1 } },
            { "after_sequence", uint64_t{ 0 } },
        });
    REQUIRE(poll.ok);
    REQUIRE(poll.value["events"].size() == 1);
    CHECK(poll.value["events"][0]["kind"] == "snapshot");
    CHECK_FALSE(poll.value.contains("server_sequence"));

    const auto invalid_client = service.handle(
        "bytes.attach",
        { { "client_id", "bad\x1b"
                         "client" } });
    CHECK_FALSE(invalid_client.ok);
    CHECK(invalid_client.error_code == "invalid_client");
}

TEST_CASE("remote terminal input backpressure is nonfatal and observable",
    "[server][remote-terminal][backpressure]")
{
    FakeTerminalRuntime runtime;
    RemoteTerminalService service(
        {
            .method_prefix = "bounded",
            .server_epoch = "bounded-epoch",
            .pane_id = "bounded-pane",
            .terminal_id = "bounded-terminal",
            .name = "Bounded",
        },
        runtime);

    const auto attached = service.handle(
        "bounded.attach", { { "client_id", "controller" } });
    REQUIRE(attached.ok);

    runtime.set_input_result(
        RemoteTerminalInputResult::Backpressure);
    const auto rejected = service.handle(
        "bounded.input",
        {
            { "client_id", "controller" },
            { "text", "queued" },
        });
    REQUIRE_FALSE(rejected.ok);
    CHECK(rejected.error_code == "backpressure");

    runtime.set_input_result(
        RemoteTerminalInputResult::Accepted);
    const auto accepted = service.handle(
        "bounded.input",
        {
            { "client_id", "controller" },
            { "text", "after-backpressure" },
        });
    REQUIRE(accepted.ok);
    CHECK(runtime.received_input() == "after-backpressure");

    service.pump();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(110));
    service.pump();
    const auto metrics = service.handle(
        "bounded.metrics", nlohmann::json::object());
    REQUIRE(metrics.ok);
    CHECK(metrics.value["sanitized"] == true);
    CHECK(metrics.value["max_loop_interval_ms"].get<uint64_t>()
        >= 100);
    CHECK(metrics.value["loop_latency_warnings"].get<uint64_t>()
        >= 1);
}

TEST_CASE("remote terminal mutation request ids are idempotent",
    "[server][remote-terminal][idempotency]")
{
    FakeTerminalRuntime runtime;
    RemoteTerminalService service(
        {
            .method_prefix = "idempotent",
            .server_epoch = "idempotent-epoch",
            .pane_id = "idempotent-pane",
            .terminal_id = "idempotent-terminal",
            .name = "Idempotent",
        },
        runtime);
    REQUIRE(service.handle("idempotent.attach",
                       { { "client_id", "controller" } })
            .ok);

    const auto first = service.handle("idempotent.input",
        {
            { "client_id", "controller" },
            { "request_id", uint64_t{ 42 } },
            { "text", "delivered-once" },
        });
    REQUIRE(first.ok);
    const auto replay = service.handle("idempotent.input",
        {
            { "client_id", "controller" },
            { "request_id", uint64_t{ 42 } },
            { "text", "must-not-be-delivered" },
        });
    REQUIRE(replay.ok);
    CHECK(replay.value == first.value);
    CHECK(runtime.received_input() == "delivered-once");
}

TEST_CASE("server terminal teardown stays off the kernel thread under input backpressure",
    "[server][remote-terminal][backpressure][shutdown]")
{
    ServerTerminalRuntimeOptions options;
#ifdef _WIN32
    options.command = "powershell.exe";
    options.args = {
        "-NoLogo",
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        "Start-Sleep -Seconds 30",
    };
#else
    options.command = "/bin/sh";
    options.args = { "-c", "sleep 30" };
#endif
    auto runtime
        = std::make_unique<ServerTerminalRuntime>(std::move(options));
    std::string error;
    REQUIRE(runtime->ensure_started(error));
    const uint64_t process_id = runtime->process_id();
    REQUIRE(process_id != 0);

    const std::string input(64 * 1024, 'x');
    bool saw_backpressure = false;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        if (runtime->send_input(input)
            == RemoteTerminalInputResult::Backpressure)
        {
            saw_backpressure = true;
            break;
        }
    }
    REQUIRE(saw_backpressure);

    const auto started = std::chrono::steady_clock::now();
    runtime.reset();
    const auto elapsed
        = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::milliseconds(100));

    const auto exit_deadline
        = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    bool process_alive = true;
    do
    {
#ifdef _WIN32
        HANDLE process = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, static_cast<DWORD>(process_id));
        if (!process)
        {
            process_alive = false;
        }
        else
        {
            DWORD exit_code = STILL_ACTIVE;
            process_alive = GetExitCodeProcess(process, &exit_code)
                && exit_code == STILL_ACTIVE;
            CloseHandle(process);
        }
#else
        process_alive = ::kill(static_cast<pid_t>(process_id), 0) == 0;
#endif
        if (process_alive)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (process_alive
        && std::chrono::steady_clock::now() < exit_deadline);
    CHECK_FALSE(process_alive);
}

TEST_CASE("a saturated non-reading terminal leaves another terminal responsive",
    "[server][remote-terminal][backpressure][multi-terminal]")
{
    ServerTerminalRuntimeOptions stalled_options;
#ifdef _WIN32
    stalled_options.command = "powershell.exe";
    stalled_options.args = {
        "-NoLogo",
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        "Start-Sleep -Seconds 30",
    };
#else
    stalled_options.command = "/bin/sh";
    stalled_options.args = { "-c", "sleep 30" };
#endif
    ServerTerminalRuntime stalled_runtime(
        std::move(stalled_options));
    ServerTerminalRuntime responsive_runtime({});
    RemoteTerminalService stalled_service(
        {
            .method_prefix = "stalled",
            .server_epoch = "multi-terminal-epoch",
            .pane_id = "stalled-pane",
            .terminal_id = "stalled-terminal",
            .name = "Stalled",
        },
        stalled_runtime);
    RemoteTerminalService responsive_service(
        {
            .method_prefix = "responsive",
            .server_epoch = "multi-terminal-epoch",
            .pane_id = "responsive-pane",
            .terminal_id = "responsive-terminal",
            .name = "Responsive",
        },
        responsive_runtime);
    REQUIRE(stalled_service.handle(
                               "stalled.attach",
                               { { "client_id", "stalled-controller" } })
            .ok);
    REQUIRE(responsive_service.handle(
                                  "responsive.attach",
                                  { { "client_id", "responsive-controller" } })
            .ok);

    const std::string input(64 * 1024, 'x');
    std::optional<std::chrono::steady_clock::duration>
        backpressure_elapsed;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        const auto started_at
            = std::chrono::steady_clock::now();
        const auto result = stalled_service.handle(
            "stalled.input",
            {
                { "client_id", "stalled-controller" },
                { "request_id", static_cast<uint64_t>(attempt + 1) },
                { "text", input },
            });
        const auto elapsed
            = std::chrono::steady_clock::now() - started_at;
        CHECK(elapsed < std::chrono::milliseconds(100));
        if (!result.ok)
        {
            REQUIRE(result.error_code == "backpressure");
            backpressure_elapsed = elapsed;
            break;
        }
    }
    REQUIRE(backpressure_elapsed);
    CHECK(*backpressure_elapsed < std::chrono::milliseconds(100));

    const auto responsive_started_at
        = std::chrono::steady_clock::now();
    const auto responsive = responsive_service.handle(
        "responsive.input",
        {
            { "client_id", "responsive-controller" },
            { "request_id", uint64_t{ 1 } },
            { "text", "still-responsive" },
        });
    const auto responsive_elapsed
        = std::chrono::steady_clock::now() - responsive_started_at;
    REQUIRE(responsive.ok);
    CHECK(responsive_elapsed < std::chrono::milliseconds(100));

    const auto metrics_started_at
        = std::chrono::steady_clock::now();
    REQUIRE(responsive_service.handle(
                                  "responsive.metrics", nlohmann::json::object())
            .ok);
    CHECK(std::chrono::steady_clock::now() - metrics_started_at
        < std::chrono::milliseconds(100));
}

namespace
{

uint64_t test_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

std::string test_process_start_token()
{
    static const std::string token = [] {
        TempDir identity_runtime(
            "draxul-server-process-identity");
        ServerKernel identity_server({
            .runtime_directory = identity_runtime.path,
            .build_version = "identity-test",
        });
        if (identity_server.start().disposition
            != ServerStartDisposition::Started)
        {
            return std::string{};
        }
        std::ifstream input(
            server_metadata_path(identity_runtime.path));
        const auto metadata
            = nlohmann::json::parse(input, nullptr, false);
        identity_server.stop();
        return metadata.is_object()
                && metadata.contains(
                    "server_process_start_token")
                && metadata["server_process_start_token"]
                       .is_string()
            ? metadata["server_process_start_token"]
                  .get<std::string>()
            : std::string{};
    }();
    return token;
}

ServerEnsureOptions probe_options(const std::filesystem::path& runtime)
{
    return {
        .runtime_directory = runtime,
        .client_id = "unit-client",
        .timeout = std::chrono::seconds(2),
        .launch_if_missing = false,
    };
}

RemoteTerminalClient remote_client(
    const std::filesystem::path& runtime,
    std::string client_id,
    std::string epoch = "fixed-epoch",
    std::string method_prefix = "fake",
    std::shared_ptr<ClientRecoveryState> recovery = {})
{
    return RemoteTerminalClient({
        .runtime_directory = runtime,
        .client_id = std::move(client_id),
        .expected_server_epoch = std::move(epoch),
        .method_prefix = std::move(method_prefix),
        .recovery = std::move(recovery),
    });
}

std::string snapshot_text(const TerminalSemanticSnapshot& snapshot)
{
    std::string text;
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
        {
            text += snapshot.cells[static_cast<size_t>(row) * snapshot.cols + col]
                        .text;
        }
        text.push_back('\n');
    }
    return text;
}

bool wait_for_text(
    RemoteTerminalClient& client, std::string_view expected,
    std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (snapshot_text(client.projection().snapshot()).find(expected)
            != std::string::npos)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    error = "Timed out waiting for terminal text.";
    return false;
}

bool wait_for_alternate_screen(
    RemoteTerminalClient& client, bool expected, std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (client.projection().snapshot().metadata.modes.alternate_screen
            == expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    error = expected
        ? "Timed out waiting to enter the alternate screen."
        : "Timed out waiting to leave the alternate screen.";
    return false;
}

bool wait_for_agent(AgentClient& client,
    std::string_view kind, std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (std::ranges::any_of(
                client.snapshot().agents,
                [kind](const ServerAgentProjection& agent) {
                    return agent.identity.kind == kind;
                }))
        {
            return true;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    }
    error = "Timed out waiting for the shared agent projection.";
    return false;
}

#ifdef _WIN32
uint64_t parent_process_id(uint64_t process_id)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uint64_t result = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == process_id)
            {
                result = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}
#endif

class ServerRunGuard
{
public:
    explicit ServerRunGuard(ServerKernel& server)
        : server_(server)
        , thread_([&server] { server.run_until_stopped(); })
    {
    }

    ~ServerRunGuard()
    {
        server_.request_stop();
    }

    void join()
    {
        server_.request_stop();
        thread_.join();
    }

private:
    ServerKernel& server_;
    std::jthread thread_;
};

class StaticRemoteTerminalRuntime final : public IRemoteTerminalRuntime
{
public:
    explicit StaticRemoteTerminalRuntime(
        TerminalSemanticSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    bool ensure_started(std::string&) override
    {
        running_ = true;
        return true;
    }
    bool restart(std::string&) override
    {
        running_ = true;
        return true;
    }
    bool pump() override
    {
        return false;
    }
    RemoteTerminalInputResult send_input(std::string_view) override
    {
        return RemoteTerminalInputResult::Accepted;
    }
    bool resize(int, int) override
    {
        return false;
    }
    bool is_running() const override
    {
        return running_;
    }
    uint64_t process_id() const override
    {
        return 1;
    }
    std::optional<int> exit_code() const override
    {
        return std::nullopt;
    }
    uint64_t scrollback_rows() const override
    {
        return 0;
    }
    std::optional<TerminalSemanticSnapshot> scrollback_page(
        uint64_t, size_t) const override
    {
        return std::nullopt;
    }
    std::optional<std::string> take_clipboard_write() override
    {
        return std::nullopt;
    }
    TerminalSemanticSnapshot snapshot() const override
    {
        return snapshot_;
    }
    TerminalDirtySnapshot take_delta() override
    {
        return {};
    }

private:
    TerminalSemanticSnapshot snapshot_;
    bool running_ = false;
};

} // namespace

TEST_CASE("queued control work wakes an otherwise idle server loop",
    "[server][control][latency]")
{
    TempDir temp("draxul-server-control-wake");
    ServerKernel server({
        .runtime_directory = temp.path,
        .idle_wait_interval = std::chrono::seconds(5),
        .epoch_override = "control-wake-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));
    const auto started = std::chrono::steady_clock::now();
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "server.status");
    const auto elapsed
        = std::chrono::steady_clock::now() - started;
    REQUIRE(response.ok);
    CHECK(elapsed < std::chrono::seconds(1));

    run_guard.join();
}

TEST_CASE("server-wide terminal allocation cap rejects topology growth",
    "[server][topology][remote-terminal][resource-bounds]")
{
    TempDir temp("draxul-server-terminal-cap");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "terminal-cap-epoch",
        .max_terminals = 4,
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "terminal-cap-client",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    const std::string space_id
        = client.snapshot().spaces.front().space_id;
    for (int index = 0; index < 3; ++index)
    {
        TopologyCommand command{
            .command_id = "terminal-cap-tab-"
                + std::to_string(index),
            .expected_revision = client.snapshot().revision,
            .kind = TopologyCommandKind::CreateTab,
            .space_id = space_id,
            .name = "Bounded shell",
            .pane_domain = TopologyPaneDomain::ServerTerminal,
        };
        TopologyCommandResult result;
        REQUIRE(client.execute(command, result, error));
    }

    TopologyCommand rejected{
        .command_id = "terminal-cap-overflow",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::CreateSpace,
        .name = "Overflow",
        .root_directory = "D:/overflow",
        .pane_domain = TopologyPaneDomain::ServerTerminal,
    };
    TopologyCommandResult result;
    CHECK_FALSE(client.execute(rejected, result, error));
    CHECK(client.last_error_code() == "terminal_start_failed");
    CHECK(error.find("terminal limit reached (4)")
        != std::string::npos);
    CHECK(server.status_snapshot().terminals == 4);

    run_guard.join();
}

TEST_CASE("server-wide scrollback cell budget rejects allocation before spawn",
    "[server][remote-terminal][resource-bounds]")
{
    TempDir temp("draxul-server-scrollback-budget");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "scrollback-budget-epoch",
        .terminal_scrollback_lines = 100,
        .max_scrollback_cells = 7'999,
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    CHECK(server.status_snapshot().scrollback_cells_reserved
        == 0);
    CHECK(server.status_snapshot().scrollback_cells_limit
        == 7'999);

    auto client = remote_client(
        temp.path, "budget-client",
        "scrollback-budget-epoch", "terminal");
    std::string error;
    REQUIRE_FALSE(client.attach(error));
    CHECK(client.last_error_code()
        == "process_start_failed");
    CHECK(error.find("scrollback memory budget")
        != std::string::npos);
    CHECK(server.status_snapshot().scrollback_cells_reserved
        == 0);

    run_guard.join();
}

TEST_CASE("restored child topology identities are scoped by their parents",
    "[server][topology][persistence][identity]")
{
    const auto tab = [](std::string name) {
        TabSnapshot result{
            .id = 0,
            .name = std::move(name),
        };
        result.pane_layout.tree.root
            = std::make_unique<SessionSplitNode>(
                SessionSplitNode{
                    .is_leaf = true,
                    .leaf_id = 0,
                });
        result.pane_layout.tree.focused_id = 0;
        result.pane_layout.tree.next_leaf_id = 1;
        result.pane_layout.panes.push_back({
            .leaf_id = 0,
            .launch = {
                .kind = HostKind::Plugin,
                .working_dir = "D:/work",
                .source_path = "custom-board",
                .client_host_kind = "plugin",
                .client_plugin_id
                    = "dev.draxul.spinning-triangle",
                .client_plugin_config_json
                    = R"({"paused":true})",
            },
            .pane_name = "Pane",
            .pane_id = "pane-0",
        });
        return result;
    };
    SessionSnapshot saved{
        .session_id = "default",
        .session_name = "Restored",
        .active_space_id = 0,
        .next_space_id = 2,
    };
    for (SpaceId space_id = 0; space_id < 2; ++space_id)
    {
        SpaceSnapshot space{
            .id = space_id,
            .name = "Space " + std::to_string(space_id),
            .active_tab_id = 0,
            .next_tab_id = 1,
        };
        space.tabs.push_back(
            tab("Tab " + std::to_string(space_id)));
        saved.spaces.push_back(std::move(space));
    }

    std::string error;
    const auto restored
        = restore_session_topology(saved, error);
    INFO(error);
    REQUIRE(restored);
    REQUIRE(restored->topology.spaces.size() == 2);
    const TopologyTab& first
        = restored->topology.spaces[0].tabs[0];
    const TopologyTab& second
        = restored->topology.spaces[1].tabs[0];
    CHECK(first.tab_id != second.tab_id);
    CHECK(first.panes[0].pane_id
        != second.panes[0].pane_id);

    const auto captured
        = capture_session_topology(
            restored->topology, error);
    INFO(error);
    REQUIRE(captured);
    const auto rerestored
        = restore_session_topology(*captured, error);
    INFO(error);
    REQUIRE(rerestored);
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .tab_id
        == first.tab_id);
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .panes[0]
              .pane_id
        == first.panes[0].pane_id);
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .panes[0]
              .client_working_directory
        == "D:/work");
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .panes[0]
              .client_source_path
        == "custom-board");
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .panes[0]
              .client_plugin_id
        == "dev.draxul.spinning-triangle");
    CHECK(rerestored->topology.spaces[0]
              .tabs[0]
              .panes[0]
              .client_plugin_config_json
        == R"({"paused":true})");
}

TEST_CASE("decorated snapshots degrade within the poll payload budget",
    "[server][remote-terminal][resource-bounds][encoding]")
{
    constexpr int kCols = 32;
    constexpr int kRows = 16;
    constexpr size_t kCellCount
        = static_cast<size_t>(kCols) * kRows;
    constexpr size_t kTestPollPayloadBudget = 12 * 1024;
    TerminalSemanticSnapshot snapshot{
        .cols = kCols,
        .rows = kRows,
    };
    snapshot.cells.reserve(kCellCount);
    for (size_t index = 0; index < kCellCount; ++index)
    {
        const uint32_t rgb = static_cast<uint32_t>(index);
        HlAttr attr;
        attr.fg = Color(
            static_cast<float>((rgb >> 16) & 0xffu) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xffu) / 255.0f,
            static_cast<float>(rgb & 0xffu) / 255.0f,
            1.0f);
        attr.has_fg = true;
        snapshot.cells.push_back({
            .text = "X",
            .attr = attr,
            .hyperlink = "https://example.test/cell/"
                + std::to_string(index),
        });
    }
    StaticRemoteTerminalRuntime runtime(std::move(snapshot));
    RemoteTerminalService service(
        {
            .method_prefix = "large",
            .server_epoch = "large-epoch",
            .pane_id = "large-pane",
            .terminal_id = "large-terminal",
            .name = "Large",
            .poll_payload_budget = kTestPollPayloadBudget,
        },
        runtime);

    const auto attached = service.handle(
        "large.attach", { { "client_id", "large-client" } });
    REQUIRE(attached.ok);
    CHECK(attached.value.dump().size() < kControlMaxMessageBytes);
    std::string error;
    const auto parsed
        = remote_terminal_attach_from_json(attached.value, error);
    INFO(error);
    REQUIRE(parsed);
    REQUIRE(parsed->state.snapshot);
    CHECK(parsed->state.snapshot->cells.size()
        == kCellCount);
    CHECK(parsed->state.snapshot->cells.front().text == "X");

    const auto metrics = service.handle(
        "large.metrics", nlohmann::json::object());
    REQUIRE(metrics.ok);
    CHECK(metrics.value["degraded_frames"].get<uint64_t>() >= 1);
    CHECK(metrics.value["poll_payload_budget"].get<size_t>()
        == kTestPollPayloadBudget);

    const auto poll = service.handle(
        "large.poll",
        {
            { "client_id", "large-client" },
            { "server_epoch", "large-epoch" },
            { "terminal_id", "large-terminal" },
            { "generation", uint64_t{ 1 } },
            { "after_sequence",
                parsed->state.version.sequence },
        });
    REQUIRE(poll.ok);
    CHECK(poll.value["events"].empty());
}

TEST_CASE("topology ratio storms retain only bounded command outcomes",
    "[server][topology][resource-bounds]")
{
    uint64_t next_terminal = 1;
    TopologyService service("cache-test", {
                                              .create_server_terminal = [&next_terminal](const ServerTerminalTopologyLaunch&, std::string&) -> std::optional<std::string> {
                                                  return "cache-terminal-"
                                                      + std::to_string(next_terminal++);
                                              },
                                          });
    const auto& initial_space = service.snapshot().spaces.front();
    const auto& initial_tab = initial_space.tabs.front();
    TopologyCommand split{
        .client_id = "cache-client",
        .command_id = "cache-split",
        .expected_revision = service.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = initial_space.space_id,
        .tab_id = initial_tab.tab_id,
        .pane_id = initial_tab.panes.front().pane_id,
        .name = "Cache split",
        .direction = TopologySplitDirection::Vertical,
        .pane_domain = TopologyPaneDomain::ServerTerminal,
    };
    REQUIRE(service.handle(
                       "topology.command",
                       topology_command_to_json(split))
            .ok);
    const std::string split_node_id
        = service.snapshot().spaces.front().tabs.front().root_node_id;

    for (size_t index = 0;
        index < kTopologyCompletedCommandLimit + 128; ++index)
    {
        TopologyCommand ratio{
            .client_id = "cache-client",
            .command_id = "cache-ratio-"
                + std::to_string(index),
            .expected_revision = service.snapshot().revision,
            .kind = TopologyCommandKind::SetSplitRatio,
            .space_id = service.snapshot().spaces.front().space_id,
            .tab_id = service.snapshot().spaces.front().tabs.front().tab_id,
            .node_id = split_node_id,
            .ratio = index % 2 == 0 ? 0.4f : 0.6f,
        };
        REQUIRE(service.handle(
                           "topology.command",
                           topology_command_to_json(ratio))
                .ok);
    }
    CHECK(service.completed_command_count()
        == kTopologyCompletedCommandLimit);
    CHECK(service.completed_command_result_bytes()
        == kTopologyCompletedCommandLimit
            * sizeof(std::string));
}

TEST_CASE("shared topology stores and updates client-local preview descriptors",
    "[server][topology][client_local][preview]")
{
    TopologyService service("client-local-preview", {});
    const TopologySpace& initial_space
        = service.snapshot().spaces.front();
    const TopologyTab& initial_tab
        = initial_space.tabs.front();
    const std::string owner_pane_id
        = initial_tab.panes.front().pane_id;
    TopologyCommand split{
        .client_id = "preview-client",
        .command_id = "preview-split",
        .expected_revision = service.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = initial_space.space_id,
        .tab_id = initial_tab.tab_id,
        .pane_id = owner_pane_id,
        .direction = TopologySplitDirection::Horizontal,
        .ratio = 2.0f / 3.0f,
        .pane_domain = TopologyPaneDomain::ClientLocal,
        .client_host_kind = "markdown",
        .client_working_directory = "D:/dev/Draxul",
        .client_source_path = "kanban/pending/one.md",
        .companion_owner_pane_id = owner_pane_id,
    };
    const auto created = service.handle(
        "topology.command", topology_command_to_json(split));
    REQUIRE(created.ok);
    const TopologyTab& split_tab
        = service.snapshot().spaces.front().tabs.front();
    REQUIRE(split_tab.panes.size() == 2);
    CHECK(split_tab.nodes.front().ratio
        == Catch::Approx(2.0f / 3.0f));
    const TopologyPane& preview = split_tab.panes.back();
    CHECK(preview.client_host_kind == "markdown");
    CHECK(preview.client_working_directory == "D:/dev/Draxul");
    CHECK(preview.client_source_path == "kanban/pending/one.md");
    CHECK(preview.companion_owner_pane_id == owner_pane_id);

    TopologyCommand update{
        .client_id = "preview-client",
        .command_id = "preview-update",
        .expected_revision = service.snapshot().revision,
        .kind = TopologyCommandKind::UpdateClientPane,
        .space_id = initial_space.space_id,
        .tab_id = initial_tab.tab_id,
        .pane_id = preview.pane_id,
        .client_host_kind = "markdown",
        .client_working_directory = "D:/dev/Draxul",
        .client_source_path = "kanban/pending/two.md",
    };
    REQUIRE(service.handle(
                       "topology.command",
                       topology_command_to_json(update))
            .ok);
    const TopologyPane& updated = service.snapshot()
                                      .spaces.front()
                                      .tabs.front()
                                      .panes.back();
    CHECK(updated.client_source_path
        == "kanban/pending/two.md");
    CHECK(updated.companion_owner_pane_id
        == owner_pane_id);
}

TEST_CASE("shared topology creates terminal-free plugin panes and validates descriptors",
    "[server][topology][client_local][plugin]")
{
    int terminal_allocations = 0;
    TopologyService service("client-local-plugin", {
        .create_server_terminal
        = [&terminal_allocations](
              const ServerTerminalTopologyLaunch&,
              std::string&) -> std::optional<std::string> {
            return "terminal-"
                + std::to_string(++terminal_allocations);
        },
    });
    const int allocations_before = terminal_allocations;
    const auto& space = service.snapshot().spaces.front();
    const auto& tab = space.tabs.front();
    TopologyCommand split{
        .client_id = "plugin-client",
        .command_id = "plugin-split",
        .expected_revision = service.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = space.space_id,
        .tab_id = tab.tab_id,
        .pane_id = tab.panes.front().pane_id,
        .direction = TopologySplitDirection::Vertical,
        .pane_domain = TopologyPaneDomain::ClientLocal,
        .client_host_kind = "plugin",
        .client_plugin_id
            = "dev.draxul.spinning-triangle",
        .client_plugin_config_json
            = R"({"paused":true})",
    };
    const auto response = service.handle(
        "topology.command", topology_command_to_json(split));
    REQUIRE(response.ok);
    CHECK(terminal_allocations == allocations_before);
    const auto& plugin = service.snapshot().spaces.front()
                             .tabs.front().panes.back();
    CHECK(plugin.domain == TopologyPaneDomain::ClientLocal);
    CHECK(plugin.terminal_id.empty());
    CHECK(plugin.client_plugin_id
        == "dev.draxul.spinning-triangle");

    TopologyCommand invalid = split;
    invalid.command_id = "plugin-invalid";
    invalid.expected_revision = service.snapshot().revision;
    invalid.client_plugin_config_json = "[]";
    const uint64_t revision_before = service.snapshot().revision;
    const auto rejected = service.handle(
        "topology.command", topology_command_to_json(invalid));
    CHECK_FALSE(rejected.ok);
    CHECK(service.snapshot().revision == revision_before);
    CHECK(terminal_allocations == allocations_before);
}

TEST_CASE("restored topology removes the legacy generated server shell name",
    "[server][topology][persistence][migration]")
{
    TopologyService original("legacy-name", {});
    TopologySnapshot legacy = original.snapshot();
    auto& pane = legacy.spaces.front().tabs.front().panes.front();
    pane.pane_id = "legacy-generated-pane-42";
    pane.terminal_id = "legacy-generated-terminal-42";
    pane.name = "Server Shell";

    TopologyService restored(std::move(legacy), {});
    CHECK(restored.snapshot().spaces.front().tabs.front().panes.front().name.empty());
}

TEST_CASE("a recorded startup failure explains an absent server", "[server][kernel]")
{
    // A detached server's stderr is /dev/null, so its startup failure must
    // reach the client through the runtime directory. The kernel writes
    // server-failed.json when start() dies; the probe surfaces the recorded
    // reason instead of an unexplained Absent.
    TempDir temp("draxul-server-failmark");
    const auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    {
        std::ofstream marker(temp.path / "server-failed.json",
            std::ios::binary | std::ios::trunc);
        marker << nlohmann::json{
            { "pid", 1234 },
            { "error", "Control socket path is too long." },
            { "created_unix_ms", now_ms },
        }
                      .dump();
    }

    const auto fresh = ServerClient::probe(probe_options(temp.path));
    REQUIRE(fresh.state == ServerProbeState::Absent);
    REQUIRE(fresh.error_code == "server_start_failed");
    REQUIRE(fresh.error_message.find("Control socket path is too long")
        != std::string::npos);

    // An old marker must not mislabel a later, unrelated problem.
    {
        std::ofstream marker(temp.path / "server-failed.json",
            std::ios::binary | std::ios::trunc);
        marker << nlohmann::json{
            { "pid", 1234 },
            { "error", "Control socket path is too long." },
            { "created_unix_ms", now_ms - 16 * 60 * 1000 },
        }
                      .dump();
    }
    const auto expired = ServerClient::probe(probe_options(temp.path));
    REQUIRE(expired.state == ServerProbeState::Absent);
    REQUIRE(expired.error_code == "endpoint_unavailable");
}

TEST_CASE("a successful start clears the failure marker and binds a short socket",
    "[server][kernel]")
{
    TempDir temp("draxul-server-failclear");
    {
        std::ofstream marker(temp.path / "server-failed.json",
            std::ios::binary | std::ios::trunc);
        marker << nlohmann::json{
            { "pid", 99 },
            { "error", "stale reason from an earlier crash" },
            { "created_unix_ms", 1 },
        }
                      .dump();
    }
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    REQUIRE_FALSE(std::filesystem::exists(temp.path / "server-failed.json"));

    // The bound socket keeps the hash-only name: sockaddr_un::sun_path is 104
    // bytes on macOS and the old <hash>-<slug> key overflowed it under a
    // normal home directory. The readable slug stays on the metadata file.
    size_t sockets = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path))
    {
        if (entry.path().extension() != ".sock")
            continue;
        ++sockets;
        REQUIRE(entry.path().filename().string().size() <= 21);
    }
#ifndef _WIN32
    REQUIRE(sockets == 1);
#endif

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    run_guard.join();
}

TEST_CASE("an evicted server retires and leaves the successor's endpoint alone",
    "[server][kernel]")
{
    // A server whose published metadata no longer names it (wiped runtime
    // dir, or a newer server claiming the path) must retire instead of
    // running forever as an unreachable tray icon — and its shutdown must
    // NOT unlink the successor's files at the shared path.
    TempDir temp("draxul-server-evict");
    ServerKernel server({
        .runtime_directory = temp.path,
        .eviction_check_interval = std::chrono::milliseconds(50),
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    const auto metadata_path = server_metadata_path(temp.path);
    REQUIRE(std::filesystem::exists(metadata_path));

    // Replace the published identity, as a successor server would.
    {
        std::ifstream input(metadata_path, std::ios::binary);
        auto metadata = nlohmann::json::parse(input);
        metadata["server_pid"]
            = metadata["server_pid"].get<uint64_t>() + 1;
        std::ofstream output(metadata_path,
            std::ios::binary | std::ios::trunc);
        output << metadata.dump();
    }

    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (server.running()
        && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(server.running());
    run_guard.join();

    // The "successor's" metadata survives the evicted server's shutdown.
    CHECK(std::filesystem::exists(metadata_path));
}

TEST_CASE("server kernel publishes one identity and stops gracefully", "[server][kernel]")
{
    TempDir temp("draxul-server-kernel");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);

    ServerRunGuard run_guard(server);
    const auto probe = ServerClient::probe(probe_options(temp.path));
    REQUIRE(probe.ready());
    REQUIRE(probe.welcome->server_pid == server.process_id());
    REQUIRE(probe.welcome->server_epoch == "fixed-epoch");
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "agent-projection-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "agent-control-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "managed-agent-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "real-remote-terminal")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "terminal-presentation-suspend-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "multi-terminal-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "named-sessions-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "session-delete-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                kServerClientTokenCapability)
        != probe.welcome->capabilities.end());
    REQUIRE_FALSE(probe.welcome->connection_token.empty());

    SessionPollRequest session_poll{
        .request_serial = 1,
        .server_epoch = probe.welcome->server_epoch,
        .terminals = {
            {
                .subscription_id = 1,
                .terminal_id = std::string(kServerShellTerminalId),
                .visibility_generation = 1,
            },
        },
    };
    auto session_poll_params
        = session_poll_request_to_json(session_poll);
    session_poll_params["session_id"] = "default";
    session_poll_params["client_id"] = "unit-client";
    session_poll_params["connection_token"]
        = probe.welcome->connection_token;
    const auto session_poll_result = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "session.poll", session_poll_params);
    REQUIRE(session_poll_result.ok);
    std::string session_poll_error;
    const auto session_poll_response
        = session_poll_response_from_json(
            session_poll_result.result, session_poll_error);
    INFO(session_poll_error);
    REQUIRE(session_poll_response);
    CHECK(session_poll_response->request_serial == 1);
    REQUIRE(session_poll_response->topology.snapshot);
    REQUIRE(session_poll_response->agents.snapshot);
    REQUIRE(session_poll_response->terminals.size() == 1);
    REQUIRE(session_poll_response->terminals.front().attach);

    const auto agents = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "agent.snapshot",
        { { "session_id", "default" } });
    REQUIRE(agents.ok);
    std::string agent_error;
    const auto agent_snapshot
        = server_agent_snapshot_from_json(
            agents.result, agent_error);
    INFO(agent_error);
    REQUIRE(agent_snapshot);
    CHECK(agent_snapshot->session_id == "default");
    CHECK(agent_snapshot->agents.empty());
    AgentClient second_agents({
        .runtime_directory = temp.path,
        .client_id = "unit-agent-observer",
    });
    REQUIRE(second_agents.refresh(agent_error));
    CHECK(second_agents.snapshot() == *agent_snapshot);

    auto recovery = std::make_shared<ClientRecoveryState>(
        "unit-client");
    REQUIRE(recovery->set_server_identity(
        probe.welcome->server_epoch,
        probe.welcome->connection_token));
    auto attached_client = remote_client(
        temp.path, "unit-client", "fixed-epoch", "fake",
        recovery);
    {
        const bool attached = attached_client.attach(agent_error);
        INFO("attach error_code=" << attached_client.last_error_code()
                                  << " message=" << agent_error);
        REQUIRE(attached);
    }
    const auto stale_topology = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "topology.poll",
        {
            { "session_id", "default" },
            { "client_id", "unit-client" },
            { "connection_token",
                probe.welcome->connection_token },
            { "after_revision",
                std::numeric_limits<uint64_t>::max() },
        });
    CHECK_FALSE(stale_topology.ok);
    CHECK(stale_topology.error_code
        == "stale_topology_revision");
    const auto stale_agents = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "agent.poll",
        {
            { "session_id", "default" },
            { "client_id", "unit-client" },
            { "connection_token",
                probe.welcome->connection_token },
            { "after_revision",
                std::numeric_limits<uint64_t>::max() },
        });
    CHECK_FALSE(stale_agents.ok);
    CHECK(stale_agents.error_code == "stale_agent_revision");

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->connected_clients == 2);
    REQUIRE(status.status->terminals == 1);
    std::string disconnect_error;
    REQUIRE(ServerClient::disconnect(
        temp.path, "unit-client", disconnect_error,
        probe.welcome->connection_token));
    const auto disconnected_status
        = ServerClient::status(temp.path);
    REQUIRE(disconnected_status.ok);
    CHECK(disconnected_status.status->connected_clients == 1);
    bool changed = false;
    CHECK_FALSE(attached_client.poll(changed, disconnect_error));
    CHECK(attached_client.last_error_code()
        == "invalid_connection_token");
    REQUIRE(ServerClient::disconnect(
        temp.path, "unit-client", disconnect_error));
    REQUIRE(ServerClient::disconnect(
        temp.path, "unit-agent-observer", disconnect_error));
    const auto fully_disconnected_status
        = ServerClient::status(temp.path);
    REQUIRE(fully_disconnected_status.ok);
    CHECK(fully_disconnected_status.status->connected_clients == 0);

    ServerKernel duplicate({
        .runtime_directory = temp.path,
    });
    REQUIRE(duplicate.start().disposition
        == ServerStartDisposition::AlreadyRunning);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    run_guard.join();
    REQUIRE_FALSE(server.running());
    REQUIRE_FALSE(std::filesystem::exists(server_metadata_path(temp.path)));
}

TEST_CASE("server connection tokens bind active client identities",
    "[server][kernel][clients][security]")
{
    TempDir temp("draxul-server-client-token");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "token-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto options = probe_options(temp.path);
    options.client_id = "bound-client";
    options.registration_nonce = "bound-registration-nonce";
    const auto probe = ServerClient::probe(options);
    REQUIRE(probe.ready());
    const std::string token
        = probe.welcome->connection_token;
    REQUIRE_FALSE(token.empty());

    const auto retried_hello = ControlClient::request(
        namespaced_control_id(
            kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "bound-client",
            .registration_nonce
            = options.registration_nonce,
            .capabilities = {
                std::string(kServerClientTokenCapability),
            },
        }));
    REQUIRE(retried_hello.ok);
    std::string welcome_error;
    const auto retried_welcome
        = server_welcome_from_json(
            retried_hello.result, welcome_error);
    INFO(welcome_error);
    REQUIRE(retried_welcome);
    CHECK(retried_welcome->connection_token == token);

    const auto request = [&](std::string_view method,
                             nlohmann::json params) {
        return ControlClient::request(
            namespaced_control_id(
                kServerControlId, temp.path),
            temp.path, method, std::move(params));
    };
    const auto missing = request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
        });
    REQUIRE_FALSE(missing.ok);
    CHECK(missing.error_code
        == "invalid_connection_token");
    const auto wrong = request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
            { "connection_token", "wrong-token" },
        });
    REQUIRE_FALSE(wrong.ok);
    CHECK(wrong.error_code
        == "invalid_connection_token");
    REQUIRE(request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
            { "connection_token", token },
        })
            .ok);

    const auto spoofed_input = request(
        "fake.input",
        {
            { "client_id", "bound-client" },
            { "request_id", uint64_t{ 1 } },
            { "text", "spoofed" },
        });
    REQUIRE_FALSE(spoofed_input.ok);
    CHECK(spoofed_input.error_code
        == "invalid_connection_token");
    REQUIRE(request(
        "fake.input",
        {
            { "client_id", "bound-client" },
            { "connection_token", token },
            { "request_id", uint64_t{ 2 } },
            { "text", "accepted" },
        })
            .ok);

    const auto wrong_hello = request(
        "server.hello",
        server_hello_to_json({
            .client_id = "bound-client",
            .connection_token = "wrong-token",
            .registration_nonce = "wrong-nonce",
            .capabilities = {
                std::string(kServerClientTokenCapability),
            },
        }));
    REQUIRE_FALSE(wrong_hello.ok);
    CHECK(wrong_hello.error_code
        == "invalid_connection_token");

    std::string error;
    REQUIRE_FALSE(ServerClient::disconnect(
        temp.path, "bound-client", error, "wrong-token"));
    const auto connected_status
        = ServerClient::status(temp.path);
    REQUIRE(connected_status.ok);
    REQUIRE(connected_status.status->connected_clients == 1);
    REQUIRE(ServerClient::disconnect(
        temp.path, "bound-client", error, token));
    const auto disconnected_status
        = ServerClient::status(temp.path);
    REQUIRE(disconnected_status.ok);
    REQUIRE(disconnected_status.status->connected_clients == 0);

    run_guard.join();

    ServerKernel replacement({
        .runtime_directory = temp.path,
        .epoch_override = "replacement-token-epoch",
    });
    REQUIRE(replacement.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard replacement_guard(replacement);
    options.connection_token = token;
    const auto replaced = ServerClient::probe(options);
    REQUIRE(replaced.ready());
    CHECK(replaced.welcome->server_epoch
        == "replacement-token-epoch");
    CHECK(replaced.welcome->connection_token != token);
    REQUIRE(ServerClient::disconnect(
        temp.path, "bound-client", error,
        replaced.welcome->connection_token));
    replacement_guard.join();
}

TEST_CASE("server releases expired terminal leases",
    "[server][kernel][clients]")
{
    TempDir temp("draxul-server-client-leases");
    ServerKernel server({
        .runtime_directory = temp.path,
        // Keep setup RPCs comfortably inside the lease on Windows debug
        // builds; the one-second sleep below still proves expiry.
        .client_activity_timeout = std::chrono::milliseconds(500),
        .build_version = "unit-test",
        .epoch_override = "lease-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto terminal = remote_client(
        temp.path, "expiring-client", "lease-epoch");
    std::string error;
    REQUIRE(terminal.attach(error));
    CHECK_FALSE(terminal.resize(
        kRemoteTerminalMaxColumns + 1, 10, error));
    CHECK(terminal.last_error_code() == "invalid_resize");
    REQUIRE(terminal.suspend(error, 1));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool changed = false;
    CHECK_FALSE(terminal.poll(changed, error));
    CHECK(terminal.last_error_code() == "not_attached");
    REQUIRE(terminal.attach(error));
    REQUIRE(terminal.send_input("reattached", error));

    REQUIRE(ServerClient::disconnect(
        temp.path, "expiring-client", error));
}

TEST_CASE("server bounds the connected client registry",
    "[server][kernel][clients]")
{
    TempDir temp("draxul-server-client-limit");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "client-limit-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    for (size_t index = 0;
        index < kServerMaxConnectedClients;
        ++index)
    {
        const auto hello = ControlClient::request(
            namespaced_control_id(
                kServerControlId, temp.path),
            temp.path, "server.hello",
            server_hello_to_json({
                .client_id = "bounded-client-"
                    + std::to_string(index),
            }));
        INFO(index);
        REQUIRE(hello.ok);
    }
    const auto rejected = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "one-client-too-many",
        }));
    CHECK_FALSE(rejected.ok);
    CHECK(rejected.error_code == "client_limit_reached");

    std::string error;
    REQUIRE(ServerClient::disconnect(
        temp.path, "bounded-client-0", error));
    const auto admitted = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "replacement-client",
        }));
    REQUIRE(admitted.ok);
}

TEST_CASE("server client classifies absent starting stale and crashed runtimes", "[server][discovery]")
{
    TempDir temp("draxul-server-discovery");
    auto options = probe_options(temp.path);
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Absent);

    const auto marker = temp.path
        / ("server-starting-" + std::to_string(test_process_id()) + ".json");
    {
        std::ofstream output(marker);
        output << nlohmann::json{
            { "pid", test_process_id() },
            { "process_start_token",
                test_process_start_token() },
            { "created_unix_ms",
                static_cast<uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                        std::chrono::system_clock::now()
                            .time_since_epoch())
                        .count()) },
        }
                      .dump();
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Starting);
    std::filesystem::remove(marker);

    {
        std::ofstream output(server_metadata_path(temp.path));
        output << "{broken";
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Stale);

    {
        std::ofstream output(server_metadata_path(temp.path));
        output << nlohmann::json{
            { "version", 1 },
            { "endpoint", R"(\\.\pipe\draxul-definitely-absent)" },
            { "token", std::string(64, 'a') },
            { "server_pid", uint64_t{ 999999999 } },
            { "server_process_start_token", "dead-process" },
            { "published_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Crashed);
}

TEST_CASE("server discovery treats a missing runtime directory as absent",
    "[server][discovery][recovery]")
{
    TempDir temp("draxul-server-missing-runtime");
    const auto runtime = temp.path / "config" / "runtime" / "server-v1";
    REQUIRE_FALSE(std::filesystem::exists(runtime));

    const auto probe = ServerClient::probe(probe_options(runtime));
    CHECK(probe.state == ServerProbeState::Absent);
    CHECK(probe.error_code == "endpoint_unavailable");
}

TEST_CASE("server discovery reports runtime inspection failures without throwing",
    "[server][discovery][recovery]")
{
    TempDir temp("draxul-server-runtime-inspection");
    const auto blocker = temp.path / "not-a-directory";
    {
        std::ofstream output(blocker);
        REQUIRE(output.good());
        output << "block runtime traversal";
    }

    auto options = probe_options(blocker / "runtime");
    ServerProbeResult probe;
    REQUIRE_NOTHROW(probe = ServerClient::probe(options));
    CHECK(probe.state == ServerProbeState::LaunchFailed);
    CHECK(probe.error_code == "runtime_unavailable");
    CHECK_FALSE(probe.error_message.empty());

    options.launch_if_missing = true;
    ServerProbeResult ensured;
    REQUIRE_NOTHROW(ensured = ServerClient::ensure(options));
    CHECK(ensured.state == ServerProbeState::LaunchFailed);
    CHECK(ensured.error_code == "runtime_unavailable");
}

TEST_CASE("server discovery rejects recycled process identities and expires starting markers",
    "[server][discovery][recovery]")
{
    TempDir temp("draxul-server-identity-recovery");
    auto options = probe_options(temp.path);
    const auto metadata = server_metadata_path(temp.path);
    {
        std::ofstream output(metadata);
        output << nlohmann::json{
            { "version", kControlProtocolVersion },
            { "endpoint",
                R"(\\.\pipe\draxul-definitely-absent)" },
            { "token", std::string(64, 'a') },
            { "server_pid", test_process_id() },
            { "server_process_start_token",
                "a-different-process-incarnation" },
            { "published_unix_ms",
                static_cast<uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                        std::chrono::system_clock::now()
                            .time_since_epoch())
                        .count()) },
        }
                      .dump();
    }
    CHECK(ServerClient::probe(options).state
        == ServerProbeState::Crashed);

    std::filesystem::remove(metadata);
    const auto expired_marker = temp.path
        / ("server-starting-"
            + std::to_string(test_process_id())
            + ".json");
    {
        std::ofstream output(expired_marker);
        output << nlohmann::json{
            { "pid", test_process_id() },
            { "process_start_token",
                test_process_start_token() },
            { "created_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    const auto expired = ServerClient::probe(options);
    CHECK(expired.state != ServerProbeState::Starting);
    CHECK_FALSE(std::filesystem::exists(expired_marker));
}

TEST_CASE("server discovery maps endpoint metadata version skew immediately",
    "[server][discovery][protocol]")
{
    TempDir temp("draxul-server-control-version");
    {
        std::ofstream output(server_metadata_path(temp.path));
        output << nlohmann::json{
            { "version", kControlProtocolVersion + 1 },
            { "endpoint",
                R"(\\.\pipe\draxul-version-skew)" },
            { "token", std::string(64, 'a') },
            { "server_pid", test_process_id() },
            { "server_process_start_token",
                test_process_start_token() },
            { "published_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    const auto probe = ServerClient::probe(
        probe_options(temp.path));
    CHECK(probe.state == ServerProbeState::Incompatible);
    CHECK(probe.error_code == "incompatible_protocol");
}

TEST_CASE("repeated control listener recreation failure stops the server kernel",
    "[server][kernel][discovery][recovery]")
{
    TempDir temp("draxul-server-listener-failure");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .listener_error_source = [] { return uint32_t{ 5 }; },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);

    CHECK(server.run_until_stopped() == 1);
    CHECK_FALSE(server.running());
    CHECK_FALSE(std::filesystem::exists(
        server_metadata_path(temp.path)));
    CHECK(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Absent);
}

TEST_CASE("server endpoint namespaces follow the runtime directory", "[server][discovery]")
{
    TempDir first("draxul-server-namespace-a");
    TempDir second("draxul-server-namespace-b");

    REQUIRE(namespaced_control_id(kServerControlId, first.path)
        == namespaced_control_id(kServerControlId,
            first.path / "." / ".." / first.path.filename()));
    REQUIRE(namespaced_control_id(kServerControlId, first.path)
        != namespaced_control_id(kServerControlId, second.path));
}

TEST_CASE("server rejects an incompatible protocol major", "[server][protocol]")
{
    TempDir temp("draxul-server-incompatible");
    ServerKernel server({
        .runtime_directory = temp.path,
        .protocol_major = kServerProtocolMajor - 1,
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    REQUIRE(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Incompatible);
    run_guard.join();
}

TEST_CASE("server client distinguishes a live but unresponsive listener", "[server][discovery]")
{
    TempDir temp("draxul-server-busy");
    ControlServer unresponsive;
    std::string start_error;
    REQUIRE(unresponsive.start(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        [] {}, &start_error,
        {
            { "server_pid", test_process_id() },
            { "server_process_start_token",
                test_process_start_token() },
            { "published_unix_ms",
                static_cast<uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                        std::chrono::system_clock::now()
                            .time_since_epoch())
                        .count()) },
        }));

    REQUIRE(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Busy);
    unresponsive.stop();
}

TEST_CASE("two remote terminal clients converge through control takeover and reconnect",
    "[server][remote-terminal]")
{
    TempDir temp("draxul-fake-remote-terminal");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client_a = remote_client(temp.path, "client-a");
    auto client_b = remote_client(temp.path, "client-b");
    std::string error;
    REQUIRE(client_a.attach(error));
    INFO(error);
    REQUIRE(client_b.attach(error));
    INFO(error);
    REQUIRE(client_a.projection().is_controller("client-a"));
    REQUIRE_FALSE(client_b.projection().is_controller("client-b"));
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE(client_a.send_input("shared", error));
    bool changed = false;
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE_FALSE(client_b.send_input("denied", error));
    REQUIRE(client_b.last_error_code() == "not_controller");
    REQUIRE(client_b.take_control(error));
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(changed);
    REQUIRE_FALSE(client_a.projection().is_controller("client-a"));
    REQUIRE(client_b.projection().is_controller("client-b"));

    REQUIRE(client_b.resize(48, 14, error));
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(client_a.projection().snapshot().cols == 48);
    REQUIRE(client_a.projection().snapshot().rows == 14);
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE(client_a.disconnect(error));
    REQUIRE(client_b.send_input("\rreconnected", error));
    REQUIRE(client_b.poll(changed, error));

    auto reconnected_a = remote_client(temp.path, "client-a");
    REQUIRE(reconnected_a.attach(error));
    INFO(error);
    REQUIRE(terminal_semantic_digest(reconnected_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));
    REQUIRE(reconnected_a.projection().version()
        == client_b.projection().version());

    run_guard.join();
}

TEST_CASE("slow remote observer resyncs without delaying the controller",
    "[server][remote-terminal]")
{
    TempDir temp("draxul-fake-remote-saturation");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto controller = remote_client(temp.path, "controller");
    auto observer = remote_client(temp.path, "observer");
    std::string error;
    REQUIRE(controller.attach(error));
    REQUIRE(observer.attach(error));
    bool changed = false;
    for (size_t index = 0; index < kRemoteTerminalQueueLimit + 8; ++index)
    {
        REQUIRE(controller.send_input("x", error));
        REQUIRE(controller.poll(changed, error));
        REQUIRE(changed);
    }

    REQUIRE(observer.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(observer.projection().version()
        == controller.projection().version());
    REQUIRE(terminal_semantic_digest(observer.projection().snapshot())
        == terminal_semantic_digest(controller.projection().snapshot()));

    run_guard.join();
}

TEST_CASE("fake endpoint shares service ack and generation resync semantics",
    "[server][remote-terminal][fake-service]")
{
    TempDir temp("draxul-fake-remote-service-parity");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fake-service-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    const auto request = [&](std::string_view method,
                             nlohmann::json params) {
        return ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path),
            temp.path, method, std::move(params));
    };

    const auto attached = request(
        "fake.attach", { { "client_id", "fake-client" } });
    REQUIRE(attached.ok);
    std::string error;
    const auto attach
        = remote_terminal_attach_from_json(attached.result, error);
    INFO(error);
    REQUIRE(attach);
    REQUIRE(attach->state.version.generation == 1);
    const auto first = request(
        "fake.input",
        {
            { "client_id", "fake-client" },
            { "request_id", uint64_t{ 1 } },
            { "text", "A" },
        });
    const auto second = request(
        "fake.input",
        {
            { "client_id", "fake-client" },
            { "request_id", uint64_t{ 2 } },
            { "text", "B" },
        });
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    const uint64_t first_sequence
        = first.result["sequence"].get<uint64_t>();
    const uint64_t second_sequence
        = second.result["sequence"].get<uint64_t>();
    REQUIRE(second_sequence == first_sequence + 1);

    const auto acknowledged = request(
        "fake.poll",
        {
            { "client_id", "fake-client" },
            { "server_epoch", "fake-service-epoch" },
            { "terminal_id", kFakeRemoteTerminalId },
            { "generation", uint64_t{ 1 } },
            { "after_sequence", first_sequence },
        });
    REQUIRE(acknowledged.ok);
    REQUIRE(acknowledged.result["events"].size() == 1);
    auto event = remote_terminal_event_from_json(
        acknowledged.result["events"][0], error);
    INFO(error);
    REQUIRE(event);
    CHECK(event->version.sequence == second_sequence);

    const auto restarted = request(
        "fake.restart",
        {
            { "client_id", "fake-client" },
            { "request_id", uint64_t{ 3 } },
        });
    REQUIRE(restarted.ok);
    REQUIRE(restarted.result["generation"] == 2);
    const auto resynced = request(
        "fake.poll",
        {
            { "client_id", "fake-client" },
            { "server_epoch", "fake-service-epoch" },
            { "terminal_id", kFakeRemoteTerminalId },
            { "generation", uint64_t{ 1 } },
            { "after_sequence", second_sequence },
        });
    REQUIRE(resynced.ok);
    REQUIRE(resynced.result["events"].size() == 1);
    event = remote_terminal_event_from_json(
        resynced.result["events"][0], error);
    INFO(error);
    REQUIRE(event);
    CHECK(event->kind == RemoteTerminalEventKind::Snapshot);
    CHECK(event->version.generation == 2);
    REQUIRE(event->snapshot);
    CHECK(snapshot_text(*event->snapshot)
              .find("Draxul remote terminal")
        != std::string::npos);
    CHECK(snapshot_text(*event->snapshot).find("AB")
        == std::string::npos);
    CHECK_FALSE(resynced.result.contains("server_sequence"));

    run_guard.join();
}

TEST_CASE("server-owned shell survives every client detaching and reconnecting",
    "[server][remote-terminal][process]")
{
    TempDir temp("draxul-real-remote-terminal");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto controller
        = remote_client(temp.path, "real-a", "fixed-epoch", "terminal");
    auto observer
        = remote_client(temp.path, "real-b", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(controller.attach(error));
    INFO(error);
    REQUIRE(observer.attach(error));
    INFO(error);

    // The pane is named after the platform's default shell: deterministic on
    // Windows, $SHELL-dependent on POSIX (Zsh on stock macOS, Bash elsewhere).
#ifdef _WIN32
    CHECK(controller.projection().pane().name == "PowerShell");
#else
    CHECK_FALSE(controller.projection().pane().name.empty());
    CHECK(controller.projection().pane().name != "PowerShell");
#endif
    const uint64_t process_id = controller.projection().pane().process_id;
    const uint64_t generation
        = controller.projection().version().generation;
    REQUIRE(process_id != 0);
    REQUIRE(observer.projection().pane().process_id == process_id);
    REQUIRE(observer.projection().version().generation == generation);
#ifdef _WIN32
    REQUIRE(parent_process_id(process_id) == server.process_id());
    const std::string shared_command
        = "Write-Output '__DRAXUL_SHARED__'\r";
    const std::string delayed_command
        = "Start-Sleep -Milliseconds 250; Write-Output '__DRAXUL_DETACHED__'\r";
#else
    const std::string shared_command
        = "printf '__DRAXUL_SHARED__\\n'\r";
    const std::string delayed_command
        = "sleep 0.25; printf '__DRAXUL_DETACHED__\\n'\r";
#endif

    REQUIRE(controller.send_input(shared_command, error));
    REQUIRE(wait_for_text(observer, "__DRAXUL_SHARED__", error));
    INFO(error);
    REQUIRE(controller.resize(72, 20, error));
    bool resized = false;
    for (int attempt = 0; attempt < 50 && !resized; ++attempt)
    {
        bool changed = false;
        REQUIRE(observer.poll(changed, error));
        resized = observer.projection().snapshot().cols == 72
            && observer.projection().snapshot().rows == 20;
        if (!resized)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(resized);

    REQUIRE(controller.send_input(delayed_command, error));
    REQUIRE(controller.disconnect(error));
    REQUIRE(observer.disconnect(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    auto reconnected
        = remote_client(temp.path, "real-c", "fixed-epoch", "terminal");
    REQUIRE(reconnected.attach(error));
    INFO(error);
    REQUIRE(reconnected.projection().pane().process_id == process_id);
    REQUIRE(reconnected.projection().version().generation == generation);
    REQUIRE(wait_for_text(reconnected, "__DRAXUL_DETACHED__", error));
    INFO(error);

    REQUIRE(reconnected.send_input("exit\r", error));
    bool exit_observed = false;
    for (int attempt = 0; attempt < 100 && !exit_observed; ++attempt)
    {
        bool changed = false;
        REQUIRE(reconnected.poll(changed, error));
        exit_observed
            = !reconnected.projection().pane().process_running
            && reconnected.projection().pane().exit_code.has_value();
        if (!exit_observed)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    REQUIRE(exit_observed);
    REQUIRE(reconnected.projection().pane().exit_code);
    CHECK(*reconnected.projection().pane().exit_code == 0);
    auto after_restart
        = remote_client(temp.path, "real-d", "fixed-epoch", "terminal");
    REQUIRE(after_restart.attach(error));
    REQUIRE(after_restart.projection().version().generation == generation + 1);
    REQUIRE(after_restart.projection().pane().process_id != 0);
    REQUIRE(server.epoch() == "fixed-epoch");

    std::string shutdown_error;
    REQUIRE_FALSE(ServerClient::shutdown(
        temp.path, {}, shutdown_error));
    CHECK(shutdown_error.find("live terminal")
        != std::string::npos);
    CHECK(server.running());
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    run_guard.join();
}

TEST_CASE("binary shell output without subscribers leaves the server running",
    "[server][remote-terminal][process][unicode]")
{
    TempDir temp("draxul-real-remote-binary");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client
        = remote_client(temp.path, "binary-a", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
    const auto completed = temp.path / "binary-output-complete";
#ifdef _WIN32
    const std::string binary_command
        = "Start-Sleep -Milliseconds 200; $b=[byte[]](0x80,0xFF); [Console]::OpenStandardOutput().Write($b,0,$b.Length); [IO.File]::WriteAllText('"
        + completed.string() + "','ok')\r";
#else
    const std::string binary_command
        = "sleep 0.2; printf '\\200\\377'; : > '"
        + completed.string() + "'\r";
#endif
    REQUIRE(client.send_input(binary_command, error));
    REQUIRE(client.disconnect(error));
    for (int attempt = 0;
        attempt < 100 && !std::filesystem::exists(completed);
        ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REQUIRE(std::filesystem::exists(completed));

    const auto probe = ServerClient::probe(probe_options(temp.path));
    INFO(probe.error_code);
    INFO(probe.error_message);
    REQUIRE(probe.ready());
    REQUIRE(server.running());

    auto reconnected
        = remote_client(temp.path, "binary-b", "fixed-epoch", "terminal");
    REQUIRE(reconnected.attach(error));
    INFO(error);

    run_guard.join();
}

TEST_CASE("clean server shell exit removes its shared pane for every client",
    "[server][remote-terminal][topology][process]")
{
    TempDir temp("draxul-remote-terminal-clean-exit");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    TopologyClient controller({
        .runtime_directory = temp.path,
        .client_id = "clean-exit-controller",
    });
    TopologyClient observer({
        .runtime_directory = temp.path,
        .client_id = "clean-exit-observer",
    });
    std::string error;
    REQUIRE(controller.refresh(error));
    REQUIRE(observer.refresh(error));
    const TopologySpace& initial_space
        = controller.snapshot().spaces.front();
    const TopologyTab& initial_tab
        = initial_space.tabs.front();
    REQUIRE_FALSE(initial_tab.name_user_set);
    TopologyCommand split{
        .command_id = "clean-exit-split",
        .expected_revision
        = controller.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = initial_space.space_id,
        .tab_id = initial_tab.tab_id,
        .pane_id = initial_tab.panes.front().pane_id,
        .name = "Disposable shell",
        .direction = TopologySplitDirection::Vertical,
        .pane_domain
        = TopologyPaneDomain::ServerTerminal,
    };
    TopologyCommandResult split_result;
    REQUIRE(controller.execute(
        split, split_result, error));
    const TopologyPane& disposable
        = split_result.snapshot.spaces.front()
              .tabs.front()
              .panes.back();
    const std::string pane_id = disposable.pane_id;
    const std::string terminal_id
        = disposable.terminal_id;

    RemoteTerminalClient terminal({
        .runtime_directory = temp.path,
        .client_id = "clean-exit-controller",
        .expected_server_epoch = "fixed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    REQUIRE(terminal.attach(error));
    REQUIRE(terminal.send_input("exit\r", error));

    const auto contains_pane
        = [&](const TopologySnapshot& snapshot) {
              for (const TopologySpace& space : snapshot.spaces)
              {
                  for (const TopologyTab& tab : space.tabs)
                  {
                      if (std::ranges::any_of(
                              tab.panes,
                              [&](const TopologyPane& pane) {
                                  return pane.pane_id
                                      == pane_id;
                              }))
                      {
                          return true;
                      }
                  }
              }
              return false;
          };
    bool removed = false;
    for (int attempt = 0;
        attempt < 200 && !removed; ++attempt)
    {
        bool controller_changed = false;
        bool observer_changed = false;
        REQUIRE(controller.poll(
            controller_changed, error));
        REQUIRE(observer.poll(
            observer_changed, error));
        removed = !contains_pane(
                      controller.snapshot())
            && !contains_pane(observer.snapshot());
        if (!removed)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    REQUIRE(removed);
    CHECK(controller.snapshot()
        == observer.snapshot());
    CHECK(controller.snapshot().spaces.front().tabs.front().panes.size()
        == 1);

    RemoteTerminalClient removed_terminal({
        .runtime_directory = temp.path,
        .client_id = "clean-exit-reconnect",
        .expected_server_epoch = "fixed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    CHECK_FALSE(removed_terminal.attach(error));
    CHECK(removed_terminal.last_error_code()
        == "terminal_not_found");
    run_guard.join();
}

TEST_CASE("server-owned shell discovery converges in two agent clients",
    "[server][agent][process]")
{
    TempDir temp("draxul-server-agent");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto terminal = remote_client(
        temp.path, "agent-terminal", "fixed-epoch",
        "terminal");
    std::string error;
    REQUIRE(terminal.attach(error));
    INFO(error);

#ifdef _WIN32
    REQUIRE(wait_for_text(terminal, "PS ", error));
    const auto source
        = std::filesystem::path(
              std::getenv("SystemRoot"))
        / "System32" / "cmd.exe";
    const auto fake_agent = temp.path / "codex.exe";
    std::filesystem::copy_file(source, fake_agent);
    const std::string command
        = "Write-Output ('__CODEX_' + 'TEST_STARTED__'); "
          "Start-Process -FilePath '"
        + fake_agent.string()
        + "' -ArgumentList '/Q','/C',"
          "'ping -t 127.0.0.1 >nul' -NoNewWindow\r";
#else
    const auto fake_agent = temp.path / "codex";
    std::filesystem::create_symlink(
        "/bin/sleep", fake_agent);
    const std::string command = "'"
        + fake_agent.string() + "' 30\r";
#endif
    REQUIRE(terminal.send_input(command, error));
#ifdef _WIN32
    REQUIRE(wait_for_text(
        terminal, "__CODEX_TEST_STARTED__", error));
#endif

    AgentClient first({
        .runtime_directory = temp.path,
        .client_id = "agent-a",
    });
    AgentClient second({
        .runtime_directory = temp.path,
        .client_id = "agent-b",
    });
    REQUIRE(first.refresh(error));
    REQUIRE(second.refresh(error));
    const bool discovered_agent
        = wait_for_agent(first, "codex", error);
    INFO("terminal screen:\n"
        << snapshot_text(terminal.projection().snapshot()));
    REQUIRE(discovered_agent);
    INFO(error);
    bool changed = false;
    for (int attempt = 0;
        attempt < 100
        && second.snapshot() != first.snapshot();
        ++attempt)
    {
        REQUIRE(second.poll(changed, error));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    }
    REQUIRE(second.snapshot() == first.snapshot());
    REQUIRE(first.snapshot().agents.size() == 1);
    CHECK(first.snapshot().agents[0].pane_id
        == kServerShellPaneId);
    CHECK(first.snapshot().agents[0].identity.origin
        == AgentIdentityOrigin::Discovered);

    const std::string instance_id
        = first.snapshot().agents[0].identity.instance_id;
    REQUIRE(terminal.disconnect(error));
    const auto server_request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    const auto listed = server_request(
        "agent.list", nlohmann::json::object());
    REQUIRE(listed.ok);
    REQUIRE(listed.result.size() == 1);
    CHECK(listed.result[0]["instance_id"] == instance_id);

    const auto waited = server_request("agent.wait",
        {
            { "instance_id", instance_id },
            { "until", { "running" } },
        });
    REQUIRE(waited.ok);
    CHECK(waited.result["complete"].get<bool>());

    const auto read = server_request("pane.read",
        {
            { "pane_id", kServerShellPaneId },
            { "lines", 20 },
        });
    REQUIRE(read.ok);
    CHECK(read.result["lines"].is_array());

    const auto sent = server_request("agent.send_text",
        {
            { "instance_id", instance_id },
            { "text", "x" },
        });
    REQUIRE(sent.ok);
    const auto keyed = server_request("agent.send_keys",
        {
            { "instance_id", instance_id },
            { "keys", { "tab" } },
        });
    REQUIRE(keyed.ok);

    const auto restarted = server_request("agent.restart",
        { { "instance_id", instance_id } });
    REQUIRE(restarted.ok);
    CHECK(restarted.result["accepted"].get<bool>());
    run_guard.join();
}

TEST_CASE("managed agents launch and restart without a UI",
    "[server][agent][managed][process]")
{
    TempDir temp("draxul-server-managed-agent");
    AgentDefinition test_agent{
        .profile_id = "test-managed",
        .kind = "codex",
        .display_name = "Managed Codex",
#ifdef _WIN32
        .executable = "powershell.exe",
        .default_args = {
            "-NoLogo",
            "-NoProfile",
            "-Command",
            "Write-Output ('__DRAXUL_AGENT_ENV__' + "
            "$env:DRAXUL_SERVER_EPOCH + ':' + "
            "$env:DRAXUL_RUNTIME_GENERATION); "
            "while ($true) { Start-Sleep -Seconds 1 }",
        },
#else
        .executable = "/bin/sh",
        .default_args = {
            "-c",
            "echo \"__DRAXUL_AGENT_ENV__"
            "$DRAXUL_SERVER_EPOCH:$DRAXUL_RUNTIME_GENERATION\"; "
            "while true; do sleep 1; done",
        },
#endif
    };
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "managed-epoch",
        .agent_definitions = { test_agent },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    const auto request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    const auto started = request("agent.start",
        {
            { "profile_id", "test-managed" },
            { "client_id", "managed-agent-a" },
            { "request_id", "managed-start-1" },
            { "cwd", temp.path.string() },
            { "args", nlohmann::json::array() },
        });
    INFO(started.error_code << ": "
                            << started.error_message);
    REQUIRE(started.ok);
    const auto replayed_start = request("agent.start",
        {
            { "profile_id", "test-managed" },
            { "client_id", "managed-agent-a" },
            { "request_id", "managed-start-1" },
            { "cwd", temp.path.string() },
            { "args", nlohmann::json::array() },
        });
    REQUIRE(replayed_start.ok);
    CHECK(replayed_start.result["instance_id"]
        == started.result["instance_id"]);
    CHECK(started.result["origin"] == "managed");
    CHECK(started.result["running"].get<bool>());
    CHECK(started.result["runtime_generation"] == 1);
    const std::string instance_id
        = started.result["instance_id"].get<std::string>();
    const std::string pane_id
        = started.result["route"]["pane_id"]
              .get<std::string>();
    const std::string terminal_id
        = started.result["route"]["terminal_id"]
              .get<std::string>();

    RemoteTerminalClient observer({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-b",
        .expected_server_epoch = "managed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    std::string terminal_error;
    REQUIRE(observer.attach(terminal_error));
    INFO(terminal_error);
    CHECK(observer.projection()
            .controller_client_id()
            .empty());
    CHECK_FALSE(observer.send_input("x", terminal_error));
    CHECK(observer.last_error_code()
        == "not_controller");

    RemoteTerminalClient launcher({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-a",
        .expected_server_epoch = "managed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    REQUIRE(launcher.attach(terminal_error));
    INFO(terminal_error);
    CHECK(launcher.projection()
              .controller_client_id()
        == "managed-agent-a");
    bool controller_changed = false;
    REQUIRE(observer.poll(
        controller_changed, terminal_error));
    CHECK(controller_changed);
    CHECK(observer.projection()
              .controller_client_id()
        == "managed-agent-a");
    REQUIRE(launcher.send_input("x", terminal_error));

    AgentClient first({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-a",
    });
    AgentClient second({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-b",
    });
    std::string agent_error;
    REQUIRE(first.refresh(agent_error));
    REQUIRE(second.refresh(agent_error));
    REQUIRE(first.snapshot() == second.snapshot());
    REQUIRE(first.snapshot().agents.size() == 1);
    CHECK(first.snapshot().agents[0].identity.instance_id == instance_id);

    auto wait_for_environment
        = [&](std::string_view expected) {
              for (int attempt = 0; attempt < 200; ++attempt)
              {
                  const auto read = request("pane.read",
                      {
                          { "pane_id", pane_id },
                          { "lines", 24 },
                      });
                  if (read.ok)
                  {
                      for (const auto& line : read.result["lines"])
                      {
                          if (line.get<std::string>()
                                  .find(expected)
                              != std::string::npos)
                          {
                              return true;
                          }
                      }
                  }
                  std::this_thread::sleep_for(
                      std::chrono::milliseconds(25));
              }
              return false;
          };
    REQUIRE(wait_for_environment(
        "__DRAXUL_AGENT_ENV__managed-epoch:1"));

    const auto reported = request(
        "pane.report_agent_session",
        {
            { "server_epoch", "managed-epoch" },
            { "runtime_generation", 1 },
            { "pane_id", pane_id },
            { "agent_instance_id", instance_id },
            { "source", "draxul:codex" },
            { "agent", "codex" },
            { "integration_version", 2 },
            { "sequence", 1 },
            { "ref_kind", "id" },
            { "ref_value", "managed-native-session" },
        });
    REQUIRE(reported.ok);

    const auto restarted = request(
        "agent.restart",
        {
            { "instance_id", instance_id },
            { "request_id", "managed-restart-1" },
        });
    REQUIRE(restarted.ok);
    CHECK(restarted.result["runtime_generation"] == 2);
    const auto replayed_restart = request(
        "agent.restart",
        {
            { "instance_id", instance_id },
            { "request_id", "managed-restart-1" },
        });
    REQUIRE(replayed_restart.ok);
    CHECK(replayed_restart.result["runtime_generation"] == 2);
    REQUIRE(wait_for_environment(
        "__DRAXUL_AGENT_ENV__managed-epoch:2"));

    const auto stale_report = request(
        "pane.report_agent_session",
        {
            { "server_epoch", "managed-epoch" },
            { "runtime_generation", 1 },
            { "pane_id", pane_id },
            { "agent_instance_id", instance_id },
            { "source", "draxul:codex" },
            { "agent", "codex" },
            { "integration_version", 2 },
            { "sequence", 2 },
            { "ref_kind", "id" },
            { "ref_value", "stale-native-session" },
        });
    CHECK_FALSE(stale_report.ok);
    CHECK(stale_report.error_code == "agent_replaced");

    const auto topology = request(
        "topology.snapshot", nlohmann::json::object());
    REQUIRE(topology.ok);
    const auto& panes = topology.result["spaces"][0]
                                       ["tabs"][0]["panes"];
    REQUIRE(panes.size() == 2);
    CHECK(panes[1]["agent"]["instance_id"]
        == instance_id);
    CHECK(panes[1]["server_working_directory"]
        == temp.path.string());
    run_guard.join();

    std::string load_error;
    const auto saved = load_session_state_from_path(
        server_session_state_path(temp.path),
        &load_error);
    INFO(load_error);
    REQUIRE(saved);
    const auto& saved_panes
        = saved->spaces.front().tabs.front().pane_layout.panes;
    const auto saved_agent = std::ranges::find(
        saved_panes, pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(saved_agent != saved_panes.end());
    REQUIRE(saved_agent->agent);
    CHECK(saved_agent->agent->instance_id
        == instance_id);
    CHECK(saved_agent->launch.working_dir
        == temp.path.string());

    ServerKernel restored({
        .runtime_directory = temp.path,
        .epoch_override = "managed-restored",
        .agent_definitions = { test_agent },
    });
    REQUIRE(restored.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard restored_guard(restored);
    const auto restored_request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    ControlClientResult restored_agent;
    for (int attempt = 0;
        attempt < 200 && !restored_agent.ok;
        ++attempt)
    {
        restored_agent = restored_request(
            "agent.get",
            { { "instance_id", instance_id } });
        if (!restored_agent.ok)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    REQUIRE(restored_agent.ok);
    CHECK(restored_agent.result["running"].get<bool>());
    CHECK(restored_agent.result["runtime_generation"] == 1);
    const std::string restored_pane_id
        = restored_agent.result["route"]["pane_id"]
              .get<std::string>();
    REQUIRE_FALSE(restored_pane_id.empty());
    CHECK(restored_pane_id != pane_id);

    bool restored_environment = false;
    for (int attempt = 0;
        attempt < 200 && !restored_environment;
        ++attempt)
    {
        const auto read = restored_request(
            "pane.read",
            {
                { "pane_id", restored_pane_id },
                { "lines", 24 },
            });
        if (read.ok)
        {
            for (const auto& line : read.result["lines"])
            {
                restored_environment
                    = restored_environment
                    || line.get<std::string>().find(
                           "__DRAXUL_AGENT_ENV__"
                           "managed-restored:1")
                        != std::string::npos;
            }
        }
        if (!restored_environment)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    CHECK(restored_environment);
    restored_guard.join();
}

TEST_CASE("two topology clients converge through idempotent server commands",
    "[server][topology]")
{
    TempDir temp("draxul-topology");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    TopologyClient first({
        .runtime_directory = temp.path,
        .client_id = "topology-a",
    });
    TopologyClient second({
        .runtime_directory = temp.path,
        .client_id = "topology-b",
    });
    std::string error;
    REQUIRE(first.refresh(error));
    REQUIRE(second.refresh(error));
    REQUIRE(first.snapshot().revision == 1);
    REQUIRE(first.snapshot().spaces.size() == 1);
    REQUIRE(first.snapshot().spaces[0].tabs[0].panes[0].domain
        == TopologyPaneDomain::ServerTerminal);
    CHECK(first.snapshot().spaces[0].tabs[0].panes[0].name.empty());

    TopologyCommand create{
        .command_id = "create-space-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::CreateSpace,
        .name = "Second",
        .root_directory = "D:/work/second",
    };
    TopologyCommandResult created;
    REQUIRE(first.execute(create, created, error));
    REQUIRE(created.applied);
    REQUIRE_FALSE(created.duplicate);
    REQUIRE(created.snapshot.revision == 2);
    REQUIRE(created.snapshot.spaces.size() == 2);
    const std::string second_space_id
        = created.snapshot.spaces.back().space_id;
    REQUIRE(created.created_id == second_space_id);

    TopologyCommandResult duplicate;
    REQUIRE(first.execute(create, duplicate, error));
    REQUIRE(duplicate.applied);
    REQUIRE(duplicate.duplicate);
    REQUIRE(duplicate.created_id == second_space_id);
    REQUIRE(duplicate.snapshot.revision == 2);

    bool changed = false;
    REQUIRE(second.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(second.snapshot() == first.snapshot());

    TopologyCommand rename{
        .command_id = "rename-space-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = second_space_id,
        .name = "Renamed",
    };
    TopologyCommandResult renamed;
    REQUIRE(first.execute(rename, renamed, error));
    REQUIRE(renamed.snapshot.revision == 3);
    REQUIRE(renamed.snapshot.spaces.back().name == "Renamed");

    REQUIRE(first.execute(create, duplicate, error));
    REQUIRE(duplicate.duplicate);
    REQUIRE(duplicate.created_id == second_space_id);
    REQUIRE(duplicate.snapshot.revision == 3);
    REQUIRE(first.snapshot().spaces.back().name == "Renamed");

    TopologyCommand stale{
        .command_id = "stale-rename",
        .expected_revision = 2,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = second_space_id,
        .name = "Stale",
    };
    TopologyCommandResult ignored;
    REQUIRE_FALSE(second.execute(stale, ignored, error));
    REQUIRE(second.last_error_code() == "revision_conflict");

    const auto& initial_space = first.snapshot().spaces.front();
    const auto& initial_tab = initial_space.tabs.front();
    const std::string initial_space_id = initial_space.space_id;
    TopologyCommand split{
        .command_id = "split-pane-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = initial_space_id,
        .tab_id = initial_tab.tab_id,
        .pane_id = initial_tab.panes.front().pane_id,
        .name = "Local editor",
        .direction = TopologySplitDirection::Horizontal,
        .pane_domain = TopologyPaneDomain::ClientLocal,
        .client_host_kind = "nvim",
    };
    TopologyCommandResult split_result;
    REQUIRE(first.execute(split, split_result, error));
    const auto& split_tab
        = split_result.snapshot.spaces.front().tabs.front();
    REQUIRE(split_tab.panes.size() == 2);
    REQUIRE(split_tab.nodes.size() == 3);
    REQUIRE(split_tab.panes.back().domain
        == TopologyPaneDomain::ClientLocal);
    REQUIRE(split_tab.panes.back().client_host_kind == "nvim");
    const std::string split_tab_id = split_tab.tab_id;
    const std::string split_target_pane_id
        = split_tab.panes.back().pane_id;
    REQUIRE(split_result.created_id
        == split_target_pane_id);

    TopologyCommand restart_client_local{
        .command_id = "restart-client-local-pane-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::RestartPane,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .pane_id = split_target_pane_id,
    };
    TopologyCommandResult rejected_restart;
    REQUIRE_FALSE(first.execute(
        restart_client_local, rejected_restart, error));
    REQUIRE(first.last_error_code() == "client_local_pane");

    TopologyCommand split_server_terminal{
        .command_id = "split-server-pane-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .pane_id = split_target_pane_id,
        .name = "Second server shell",
        .direction = TopologySplitDirection::Vertical,
        .pane_domain = TopologyPaneDomain::ServerTerminal,
    };
    TopologyCommandResult server_split_result;
    REQUIRE(first.execute(
        split_server_terminal, server_split_result, error));
    const TopologyPane& server_pane
        = server_split_result.snapshot.spaces.front()
              .tabs.front()
              .panes.back();
    REQUIRE(server_pane.domain
        == TopologyPaneDomain::ServerTerminal);
    REQUIRE_FALSE(server_pane.terminal_id.empty());
    REQUIRE(server_pane.terminal_id
        != kServerShellTerminalId);
    const std::string dynamic_pane_id = server_pane.pane_id;
    const std::string dynamic_terminal_id
        = server_pane.terminal_id;

    RemoteTerminalClient dynamic_terminal({
        .runtime_directory = temp.path,
        .client_id = "dynamic-terminal-client",
        .expected_server_epoch = "fixed-epoch",
        .method_prefix = "terminal",
        .terminal_id = dynamic_terminal_id,
    });
    REQUIRE(dynamic_terminal.attach(error));
    REQUIRE(dynamic_terminal.projection().pane().pane_id
        == dynamic_pane_id);
    REQUIRE(dynamic_terminal.projection().pane().terminal_id
        == dynamic_terminal_id);
    const uint64_t dynamic_generation
        = dynamic_terminal.projection().version().generation;
    const uint64_t dynamic_process_id
        = dynamic_terminal.projection().pane().process_id;

    TopologyCommand restart_server_terminal{
        .command_id = "restart-server-pane-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::RestartPane,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .pane_id = dynamic_pane_id,
    };
    TopologyCommandResult restarted_server_terminal;
    REQUIRE(first.execute(restart_server_terminal,
        restarted_server_terminal, error));
    bool terminal_changed = false;
    REQUIRE(dynamic_terminal.poll(terminal_changed, error));
    REQUIRE(terminal_changed);
    REQUIRE(dynamic_terminal.projection().version().generation
        == dynamic_generation + 1);
    REQUIRE(dynamic_terminal.projection().pane().process_id != 0);
    REQUIRE(dynamic_terminal.projection().pane().process_id
        != dynamic_process_id);

    TopologyCommandResult duplicate_restart;
    REQUIRE(first.execute(restart_server_terminal,
        duplicate_restart, error));
    REQUIRE(duplicate_restart.duplicate);
    REQUIRE(dynamic_terminal.poll(terminal_changed, error));
    REQUIRE(dynamic_terminal.projection().version().generation
        == dynamic_generation + 1);
    REQUIRE(dynamic_terminal.disconnect(error));

    TopologyClient reconnected_topology({
        .runtime_directory = temp.path,
        .client_id = "topology-reconnected",
    });
    REQUIRE(reconnected_topology.refresh(error));
    REQUIRE(reconnected_topology.snapshot()
        == first.snapshot());

    RemoteTerminalClient reconnected_dynamic_terminal({
        .runtime_directory = temp.path,
        .client_id = "dynamic-terminal-reconnected",
        .expected_server_epoch = "fixed-epoch",
        .method_prefix = "terminal",
        .terminal_id = dynamic_terminal_id,
    });
    REQUIRE(reconnected_dynamic_terminal.attach(error));
    REQUIRE(reconnected_dynamic_terminal.projection()
                .version()
                .generation
        == dynamic_generation + 1);
    REQUIRE(reconnected_dynamic_terminal.projection()
                .pane()
                .process_id
        == dynamic_terminal.projection().pane().process_id);
    REQUIRE(reconnected_dynamic_terminal.disconnect(error));

    TopologyCommand close_server_terminal{
        .command_id = "close-server-pane-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::ClosePane,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .pane_id = dynamic_pane_id,
    };
    TopologyCommandResult closed_server_terminal;
    REQUIRE(first.execute(close_server_terminal,
        closed_server_terminal, error));
    REQUIRE(closed_server_terminal.snapshot.spaces.front()
                .tabs.front()
                .panes.size()
        == 2);

    RemoteTerminalClient removed_terminal({
        .runtime_directory = temp.path,
        .client_id = "removed-terminal-client",
        .expected_server_epoch = "fixed-epoch",
        .method_prefix = "terminal",
        .terminal_id = dynamic_terminal_id,
    });
    REQUIRE_FALSE(removed_terminal.attach(error));
    REQUIRE(removed_terminal.last_error_code()
        == "terminal_not_found");

    const auto& two_pane_tab
        = first.snapshot().spaces.front().tabs.front();
    REQUIRE(two_pane_tab.panes.size() == 2);
    const std::string root_node_id = two_pane_tab.root_node_id;
    const std::string first_leaf_pane_id
        = two_pane_tab.nodes[1].pane_id;
    const std::string second_leaf_pane_id
        = two_pane_tab.nodes[2].pane_id;

    TopologyCommand resize_split{
        .command_id = "resize-split-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::SetSplitRatio,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .node_id = root_node_id,
        .ratio = 0.7f,
    };
    TopologyCommandResult resized;
    REQUIRE(first.execute(resize_split, resized, error));
    REQUIRE(resized.snapshot.spaces.front()
                .tabs.front()
                .nodes.front()
                .ratio
        == Catch::Approx(0.7f));

    TopologyCommand swap_panes{
        .command_id = "swap-panes-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::SwapPane,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
        .pane_id = first_leaf_pane_id,
        .target_pane_id = second_leaf_pane_id,
    };
    TopologyCommandResult swapped;
    REQUIRE(first.execute(swap_panes, swapped, error));
    const auto& swapped_nodes
        = swapped.snapshot.spaces.front().tabs.front().nodes;
    REQUIRE(swapped_nodes[1].pane_id == second_leaf_pane_id);
    REQUIRE(swapped_nodes[2].pane_id == first_leaf_pane_id);

    TopologyCommand equalize{
        .command_id = "equalize-splits-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::EqualizeSplits,
        .space_id = initial_space_id,
        .tab_id = split_tab_id,
    };
    TopologyCommandResult equalized;
    REQUIRE(first.execute(equalize, equalized, error));
    REQUIRE(equalized.snapshot.spaces.front()
                .tabs.front()
                .nodes.front()
                .ratio
        == Catch::Approx(0.5f));

    TopologyCommand create_tab{
        .command_id = "create-tab-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::CreateTab,
        .space_id = initial_space_id,
        .name = "PowerShell",
        .client_host_kind = "powershell",
    };
    TopologyCommandResult tab_result;
    REQUIRE(first.execute(create_tab, tab_result, error));
    const auto& created_tab
        = tab_result.snapshot.spaces.front().tabs.back();
    REQUIRE(tab_result.created_id == created_tab.tab_id);
    REQUIRE(created_tab.name == "PowerShell");
    REQUIRE_FALSE(created_tab.name_user_set);
    REQUIRE(created_tab.panes.size() == 1);
    REQUIRE(created_tab.panes.front().domain
        == TopologyPaneDomain::ClientLocal);
    REQUIRE(created_tab.panes.front().client_host_kind
        == "powershell");

    TopologyCommand rename_tab{
        .command_id = "rename-tab-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::RenameTab,
        .space_id = initial_space_id,
        .tab_id = created_tab.tab_id,
        .name = "Shared PowerShell",
    };
    TopologyCommandResult renamed_tab;
    REQUIRE(first.execute(rename_tab, renamed_tab, error));
    REQUIRE(renamed_tab.snapshot.spaces.front().tabs.back().name
        == "Shared PowerShell");
    REQUIRE(renamed_tab.snapshot.spaces.front()
            .tabs.back()
            .name_user_set);

    TopologyCommand move_tab{
        .command_id = "move-tab-1",
        .expected_revision = first.snapshot().revision,
        .kind = TopologyCommandKind::MoveTab,
        .space_id = initial_space_id,
        .tab_id = created_tab.tab_id,
        .move_delta = -1,
    };
    TopologyCommandResult moved_tab;
    REQUIRE(first.execute(move_tab, moved_tab, error));
    REQUIRE(moved_tab.snapshot.spaces.front().tabs.front().tab_id
        == created_tab.tab_id);

    REQUIRE(second.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(second.snapshot() == first.snapshot());

    run_guard.join();
}

TEST_CASE("server-owned shell exposes bounded client-independent scrollback pages",
    "[server][remote-terminal][process][scrollback]")
{
    TempDir temp("draxul-real-remote-scrollback");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto first
        = remote_client(temp.path, "scroll-a", "fixed-epoch", "terminal");
    auto second
        = remote_client(temp.path, "scroll-b", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(first.attach(error));
    REQUIRE(second.attach(error));
    REQUIRE(first.resize(40, 8, error));
#ifdef _WIN32
    const std::string command
        = "1..40 | ForEach-Object { Write-Output (\"__SB_{0:D2}__\" -f $_) }\r";
#else
    const std::string command
        = "for i in $(seq 1 40); do printf '__SB_%02d__\\n' \"$i\"; done\r";
#endif
    REQUIRE(first.send_input(command, error));
    REQUIRE(wait_for_text(first, "__SB_40__", error));
    INFO(error);

    bool changed = false;
    REQUIRE(second.poll(changed, error));
    const uint64_t live_digest
        = terminal_semantic_digest(second.projection().snapshot());

    RemoteTerminalScrollbackPage near_live;
    REQUIRE(first.read_scrollback(5, 8, near_live, error));
    INFO(error);
    REQUIRE(near_live.total_rows >= 5);
    REQUIRE(near_live.offset_from_live == 5);
    REQUIRE(near_live.snapshot.has_value());
    REQUIRE(near_live.snapshot->rows == 5);
    REQUIRE(near_live.snapshot->cols == 40);

    RemoteTerminalScrollbackPage farther_back;
    REQUIRE(second.read_scrollback(10, 8, farther_back, error));
    INFO(error);
    REQUIRE(farther_back.total_rows == near_live.total_rows);
    REQUIRE(farther_back.offset_from_live == 10);
    REQUIRE(farther_back.snapshot.has_value());
    REQUIRE(farther_back.snapshot->rows == 8);
    REQUIRE(terminal_semantic_digest(second.projection().snapshot())
        == live_digest);

    const auto metrics = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        "terminal.metrics");
    REQUIRE(metrics.ok);
    REQUIRE(metrics.result["sanitized"] == true);
    REQUIRE(metrics.result["delta_frames"].get<uint64_t>() > 0);
    REQUIRE(metrics.result["delta_cells"].get<uint64_t>() > 0);
    REQUIRE(metrics.result["full_frame_cells"].get<uint64_t>()
        >= metrics.result["delta_cells"].get<uint64_t>());
    REQUIRE(metrics.result["scrollback_requests"].get<uint64_t>() >= 2);
    REQUIRE(metrics.result["scrollback_rows_served"].get<uint64_t>() >= 13);
    REQUIRE_FALSE(metrics.result.contains("text"));
    REQUIRE_FALSE(metrics.result.contains("cells"));

    run_guard.join();
}

TEST_CASE("suspended remote terminal presentation avoids deltas and resumes from a snapshot",
    "[server][remote-terminal][suspend]")
{
    TempDir temp("draxul-real-remote-suspend");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client = remote_client(
        temp.path, "suspend-controller", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
    REQUIRE(client.projection().is_controller("suspend-controller"));
    REQUIRE(client.resize(40, 8, error, 1));
    const auto version_before_suspend = client.projection().version();
    REQUIRE(client.suspend(error, 2));

    auto metrics = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        "terminal.metrics");
    REQUIRE(metrics.ok);
    REQUIRE(metrics.result["active_subscribers"] == 0);
    REQUIRE(metrics.result["suspended_subscribers"] == 1);
    REQUIRE(metrics.result["suspensions"] == 1);

#ifdef _WIN32
    const std::string command
        = "1..40 | ForEach-Object { Write-Output (\"__SUSPEND_{0:D2}__\" -f $_); Start-Sleep -Milliseconds 25 }\r";
#else
    const std::string command
        = "for i in $(seq 1 40); do printf '__SUSPEND_%02d__\\n' \"$i\"; sleep 0.025; done\r";
#endif
    REQUIRE(client.send_input(command, error, 3));

    bool avoided_delta = false;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        metrics = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(metrics.ok);
        if (metrics.result["avoided_delta_encodes"].get<uint64_t>() > 32)
        {
            avoided_delta = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(avoided_delta);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    bool changed = false;
    REQUIRE_FALSE(client.poll(changed, error));
    REQUIRE(client.last_error_code() == "suspended");
    REQUIRE(client.resume(error));
    REQUIRE(client.projection().is_controller("suspend-controller"));
    REQUIRE(client.projection().version().sequence
        > version_before_suspend.sequence);

    auto verifier = remote_client(
        temp.path, "suspend-verifier", "fixed-epoch", "terminal");
    REQUIRE(verifier.attach(error));
    bool converged = false;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool client_changed = false;
        bool verifier_changed = false;
        REQUIRE(client.poll(client_changed, error));
        REQUIRE(verifier.poll(verifier_changed, error));
        if (client.projection().version()
                == verifier.projection().version()
            && terminal_semantic_digest(client.projection().snapshot())
                == terminal_semantic_digest(
                    verifier.projection().snapshot()))
        {
            converged = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(converged);

    metrics = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        "terminal.metrics");
    REQUIRE(metrics.ok);
    REQUIRE(metrics.result["active_subscribers"] == 2);
    REQUIRE(metrics.result["suspended_subscribers"] == 0);
    REQUIRE(metrics.result["resumes"] == 1);

    auto observer = remote_client(
        temp.path, "suspend-observer", "fixed-epoch", "terminal");
    REQUIRE(observer.attach(error));
    REQUIRE(client.suspend(error, 4));
#ifdef _WIN32
    const std::string observer_command
        = "Write-Output '__ACTIVE_OBSERVER_READY__'\r";
#else
    const std::string observer_command
        = "printf '__ACTIVE_OBSERVER_READY__\\n'\r";
#endif
    REQUIRE(client.send_input(observer_command, error, 5));
    REQUIRE(wait_for_text(observer, "__ACTIVE_OBSERVER_READY__", error));
    INFO(error);
    metrics = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        "terminal.metrics");
    REQUIRE(metrics.ok);
    REQUIRE(metrics.result["active_subscribers"] == 2);
    REQUIRE(metrics.result["suspended_subscribers"] == 1);
    REQUIRE(observer.take_control(error, 6));
    REQUIRE(observer.poll(changed, error));
    REQUIRE(observer.projection().is_controller("suspend-observer"));
    REQUIRE(client.resume(error));
    REQUIRE_FALSE(client.projection().is_controller(
        "suspend-controller"));

    run_guard.join();
}

TEST_CASE("server-owned shell rejects unsupported launch kinds clearly",
    "[server][remote-terminal][process][config]")
{
    TempDir temp("draxul-real-remote-invalid-shell");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
        .terminal_shell_kind = "unsupported-shell",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client
        = remote_client(temp.path, "invalid-shell", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE_FALSE(client.attach(error));
    REQUIRE(client.last_error_code() == "process_start_failed");
    REQUIRE(error.find("Unsupported Draxul server shell kind")
        != std::string::npos);

    run_guard.join();
}

TEST_CASE("named server Sessions isolate topology and terminal identity across cold restore",
    "[server][topology][persistence][sessions]")
{
    TempDir temp("draxul-server-named-sessions");
    uint64_t alpha_process_id = 0;
    uint64_t beta_process_id = 0;

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "named-epoch-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-client",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-client",
            .session_id = "beta",
        });
        std::string error;
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));
        REQUIRE(alpha.snapshot().session_id == "alpha");
        REQUIRE(beta.snapshot().session_id == "beta");
        REQUIRE(alpha.snapshot().spaces.front().tabs.front().panes.front().terminal_id
            == beta.snapshot().spaces.front().tabs.front().panes.front().terminal_id);

        TopologyCommand rename_alpha{
            .command_id = "rename-alpha",
            .expected_revision = alpha.snapshot().revision,
            .kind = TopologyCommandKind::RenameSpace,
            .space_id = alpha.snapshot().spaces.front().space_id,
            .name = "Alpha Work",
        };
        TopologyCommandResult renamed_alpha;
        REQUIRE(alpha.execute(
            rename_alpha, renamed_alpha, error));
        REQUIRE(beta.snapshot().spaces.front().name == "Space 1");

        TopologyCommand rename_beta{
            .command_id = "rename-beta",
            .expected_revision = beta.snapshot().revision,
            .kind = TopologyCommandKind::RenameSpace,
            .space_id = beta.snapshot().spaces.front().space_id,
            .name = "Beta Work",
        };
        TopologyCommandResult renamed_beta;
        REQUIRE(beta.execute(rename_beta, renamed_beta, error));
        REQUIRE(alpha.snapshot().spaces.front().name
            == "Alpha Work");

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-terminal-client",
            .session_id = "alpha",
            .expected_server_epoch = "named-epoch-1",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-terminal-client",
            .session_id = "beta",
            .expected_server_epoch = "named-epoch-1",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        alpha_process_id
            = alpha_terminal.projection().pane().process_id;
        beta_process_id
            = beta_terminal.projection().pane().process_id;
        REQUIRE(alpha_process_id != 0);
        REQUIRE(beta_process_id != 0);
        REQUIRE(alpha_process_id != beta_process_id);

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        REQUIRE(status.status->sessions == 3);
        REQUIRE(status.status->spaces == 3);
        REQUIRE(status.status->session_statuses.size() == 3);
        const auto alpha_status = std::ranges::find(
            status.status->session_statuses, "alpha",
            &ServerSessionStatusSnapshot::session_id);
        REQUIRE(alpha_status
            != status.status->session_statuses.end());
        REQUIRE(alpha_status->terminals == 1);
        REQUIRE(alpha_status->live_terminals == 1);

        run_guard.join();
    }

    const auto alpha_path
        = server_session_state_path(temp.path, "alpha");
    const auto beta_path
        = server_session_state_path(temp.path, "beta");
    REQUIRE(alpha_path != beta_path);
    REQUIRE(std::filesystem::exists(alpha_path));
    REQUIRE(std::filesystem::exists(beta_path));
    std::string error;
    const auto saved_alpha
        = load_session_state_from_path(alpha_path, &error);
    INFO(error);
    REQUIRE(saved_alpha);
    REQUIRE(saved_alpha->session_id == "alpha");
    const auto saved_beta
        = load_session_state_from_path(beta_path, &error);
    INFO(error);
    REQUIRE(saved_beta);
    REQUIRE(saved_beta->session_id == "beta");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "named-epoch-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-restored",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-restored",
            .session_id = "beta",
        });
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));
        REQUIRE(alpha.snapshot().spaces.front().name
            == "Alpha Work");
        REQUIRE(beta.snapshot().spaces.front().name
            == "Beta Work");

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-terminal-restored",
            .session_id = "alpha",
            .expected_server_epoch = "named-epoch-2",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-terminal-restored",
            .session_id = "beta",
            .expected_server_epoch = "named-epoch-2",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        REQUIRE(alpha_terminal.projection().pane().process_id
            != alpha_process_id);
        REQUIRE(beta_terminal.projection().pane().process_id
            != beta_process_id);
        REQUIRE(alpha_terminal.projection().pane().process_id
            != beta_terminal.projection().pane().process_id);

        TopologyClient invalid({
            .runtime_directory = temp.path,
            .client_id = "invalid-session",
            .session_id = "invalid\nidentity",
        });
        REQUIRE_FALSE(invalid.refresh(error));
        REQUIRE(invalid.last_error_code() == "invalid_session");
        run_guard.join();
    }
}

TEST_CASE("server deletes a detached Session and its checkpoint",
    "[server][topology][persistence][sessions][delete]")
{
    TempDir temp("draxul-server-delete-session");
    const auto alpha_checkpoint
        = server_session_state_path(temp.path, "alpha");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "delete-session-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
        });
        std::string error;
        REQUIRE(alpha.refresh(error));

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
            .expected_server_epoch = "delete-session-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));

        for (int attempt = 0;
            attempt < 100
            && !std::filesystem::exists(alpha_checkpoint);
            ++attempt)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        REQUIRE(std::filesystem::exists(alpha_checkpoint));

        REQUIRE(ServerClient::rename_session(
            temp.path, "alpha", "Renamed Alpha", error));
        const auto renamed_status
            = ServerClient::status(temp.path);
        REQUIRE(renamed_status.ok);
        const auto renamed_session = std::ranges::find(
            renamed_status.status->session_statuses,
            std::string("alpha"),
            &ServerSessionStatusSnapshot::session_id);
        REQUIRE(renamed_session
            != renamed_status.status->session_statuses.end());
        CHECK(renamed_session->session_name
            == "Renamed Alpha");
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const auto current
                = ServerClient::status(temp.path);
            REQUIRE(current.ok);
            const auto alpha_status = std::ranges::find(
                current.status->session_statuses,
                std::string("alpha"),
                &ServerSessionStatusSnapshot::session_id);
            REQUIRE(alpha_status
                != current.status->session_statuses.end());
            if (alpha_status->checkpoint_state == "ok")
                break;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        auto renamed_checkpoint = load_session_state_from_path(
            alpha_checkpoint, &error);
        INFO(error);
        REQUIRE(renamed_checkpoint);
        CHECK(renamed_checkpoint->session_name
            == "Renamed Alpha");

        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("still attached")
            != std::string::npos);

        REQUIRE(ServerClient::disconnect(
            temp.path, "alpha-ui", error));
        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha", {}, error));
        CHECK(error.find("Retry with --yes")
            != std::string::npos);

        REQUIRE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.empty());
        CHECK_FALSE(std::filesystem::exists(alpha_checkpoint));

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        CHECK(std::ranges::none_of(
            status.status->session_statuses,
            [](const auto& session) {
                return session.session_id == "alpha";
            }));

        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("does not exist")
            != std::string::npos);
        run_guard.join();
    }

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "delete-session-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        CHECK(status.status->session_statuses.front()
                  .session_id
            == "default");
        run_guard.join();
    }
}

TEST_CASE("server deletes all detached Sessions and stops their terminals",
    "[server][topology][persistence][sessions][delete-all]")
{
    TempDir temp("draxul-server-delete-all-sessions");
    const auto alpha_checkpoint
        = server_session_state_path(temp.path, "alpha");
    const auto beta_checkpoint
        = server_session_state_path(temp.path, "beta");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "delete-all-sessions-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        std::string error;
        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-ui",
            .session_id = "beta",
        });
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
            .expected_server_epoch
            = "delete-all-sessions-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-ui",
            .session_id = "beta",
            .expected_server_epoch
            = "delete-all-sessions-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        REQUIRE(ServerClient::rename_session(
            temp.path, "alpha", "Alpha", error));
        REQUIRE(ServerClient::rename_session(
            temp.path, "beta", "Beta", error));
        REQUIRE(std::filesystem::exists(alpha_checkpoint));
        REQUIRE(std::filesystem::exists(beta_checkpoint));

        REQUIRE_FALSE(ServerClient::delete_all_sessions(
            temp.path,
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("still attached")
            != std::string::npos);

        REQUIRE(ServerClient::disconnect(
            temp.path, "alpha-ui", error));
        REQUIRE(ServerClient::disconnect(
            temp.path, "beta-ui", error));
        REQUIRE_FALSE(ServerClient::delete_all_sessions(
            temp.path, {}, error));
        CHECK(error.find("--yes") != std::string::npos);

        REQUIRE(ServerClient::delete_all_sessions(
            temp.path,
            { .confirm_live_terminals = true }, error));
        CHECK(error.empty());
        CHECK_FALSE(std::filesystem::exists(alpha_checkpoint));
        CHECK_FALSE(std::filesystem::exists(beta_checkpoint));

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 0);
        CHECK(status.status->terminals == 0);
        CHECK(status.status->session_statuses.empty());
        run_guard.join();
    }

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "delete-all-sessions-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        REQUIRE(status.status->session_statuses.size() == 1);
        CHECK(status.status->session_statuses.front()
                  .session_id
            == "default");
        run_guard.join();
    }
}

TEST_CASE("server topology checkpoints and cold-restores stable terminal descriptors",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    std::string dynamic_terminal_id;
    std::string dynamic_pane_id;
    std::string restored_dynamic_pane_id;
    uint64_t original_process_id = 0;

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .epoch_override = "persistence-first",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        TopologyClient client({
            .runtime_directory = temp.path,
            .client_id = "persistence-writer",
        });
        std::string error;
        REQUIRE(client.refresh(error));

        TopologyCommand create_space{
            .command_id = "persist-space",
            .expected_revision = client.snapshot().revision,
            .kind = TopologyCommandKind::CreateSpace,
            .name = "Restored Space",
            .root_directory = "D:/restored",
        };
        TopologyCommandResult created;
        REQUIRE(client.execute(create_space, created, error));

        const auto& first_space = client.snapshot().spaces.front();
        const auto& first_tab = first_space.tabs.front();
        TopologyCommand split{
            .command_id = "persist-terminal",
            .expected_revision = client.snapshot().revision,
            .kind = TopologyCommandKind::SplitPane,
            .space_id = first_space.space_id,
            .tab_id = first_tab.tab_id,
            .pane_id = first_tab.panes.front().pane_id,
            .name = "Persistent Shell",
            .direction = TopologySplitDirection::Horizontal,
            .pane_domain = TopologyPaneDomain::ServerTerminal,
        };
        TopologyCommandResult split_result;
        REQUIRE(client.execute(split, split_result, error));
        const TopologyPane& dynamic
            = split_result.snapshot.spaces.front()
                  .tabs.front()
                  .panes.back();
        dynamic_terminal_id = dynamic.terminal_id;
        dynamic_pane_id = dynamic.pane_id;

        RemoteTerminalClient terminal({
            .runtime_directory = temp.path,
            .client_id = "persistence-terminal-first",
            .expected_server_epoch = "persistence-first",
            .method_prefix = "terminal",
            .terminal_id = dynamic_terminal_id,
        });
        REQUIRE(terminal.attach(error));
        original_process_id = terminal.projection().pane().process_id;
        REQUIRE(original_process_id != 0);
        run_guard.join();
    }

    REQUIRE(std::filesystem::exists(checkpoint));
    std::string load_error;
    auto saved = load_session_state_from_path(
        checkpoint, &load_error);
    INFO(load_error);
    REQUIRE(saved);
    REQUIRE(saved->spaces.size() == 2);
    REQUIRE(saved->spaces.back().name == "Restored Space");
    REQUIRE_FALSE(saved->spaces.front()
            .tabs.front()
            .name_user_set);
    auto& saved_panes
        = saved->spaces.front().tabs.front().pane_layout.panes;
    const auto saved_dynamic = std::ranges::find(
        saved_panes, dynamic_pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(saved_dynamic != saved_panes.end());
    saved_dynamic->agent = AgentIdentity{
        .profile_id = "codex",
        .kind = "codex",
        .display_name = "Codex",
        .instance_id = "persisted-agent",
    };
    saved_dynamic->agent_session = AgentSessionRef{
        .source = "draxul:codex",
        .agent_kind = "codex",
        .integration_version = 1,
        .sequence = 1,
        .kind = AgentSessionRefKind::Id,
        .value = "persisted-session",
    };
    saved_dynamic->restore_policy
        = AgentRestorePolicy::ShellOnly;
    REQUIRE(save_session_state_to_path(
        *saved, checkpoint, &load_error));
    const auto checkpoint_time
        = std::filesystem::last_write_time(checkpoint);

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .epoch_override = "persistence-second",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        // Wrapped in extra parens so Catch2 does not decompose the expression:
        // file_time_type has a __int128 duration rep that it cannot stringify.
        REQUIRE((std::filesystem::last_write_time(checkpoint)
            == checkpoint_time));
        ServerRunGuard run_guard(server);
        TopologyClient client({
            .runtime_directory = temp.path,
            .client_id = "persistence-reader",
        });
        std::string error;
        REQUIRE(client.refresh(error));
        REQUIRE(client.snapshot().spaces.size() == 2);
        REQUIRE(client.snapshot().spaces.back().name
            == "Restored Space");
        REQUIRE(client.snapshot().spaces.back().root_directory
            == "D:/restored");
        REQUIRE_FALSE(client.snapshot().spaces.front().tabs.front().name_user_set);
        REQUIRE(client.snapshot().spaces.back().tabs.front().panes.front().domain
            == TopologyPaneDomain::ClientLocal);
        REQUIRE(client.snapshot().spaces.back().tabs.front().panes.front().client_host_kind
            == "platform_default");

        const auto& restored_panes
            = client.snapshot().spaces.front().tabs.front().panes;
        const auto dynamic = std::ranges::find(
            restored_panes, dynamic_terminal_id,
            &TopologyPane::terminal_id);
        REQUIRE(dynamic != restored_panes.end());
        restored_dynamic_pane_id = dynamic->pane_id;
        REQUIRE_FALSE(restored_dynamic_pane_id.empty());
        REQUIRE(restored_dynamic_pane_id
            != dynamic_pane_id);
        REQUIRE(dynamic->agent);
        REQUIRE(dynamic->agent->instance_id
            == "persisted-agent");
        REQUIRE(dynamic->agent_session);
        REQUIRE(dynamic->agent_session->value
            == "persisted-session");
        REQUIRE(dynamic->restore_policy
            == AgentRestorePolicy::ShellOnly);

        RemoteTerminalClient terminal({
            .runtime_directory = temp.path,
            .client_id = "persistence-terminal-second",
            .expected_server_epoch = "persistence-second",
            .method_prefix = "terminal",
            .terminal_id = dynamic_terminal_id,
        });
        REQUIRE(terminal.attach(error));
        REQUIRE(terminal.projection().pane().process_id != 0);
        REQUIRE(terminal.projection().pane().process_id
            != original_process_id);
        REQUIRE(terminal.projection().version().generation == 1);
        REQUIRE(terminal.disconnect(error));

        const auto report
            = [&](std::string_view epoch,
                  uint64_t generation,
                  uint64_t sequence) {
                  return ControlClient::request(
                      namespaced_control_id(
                          kServerControlId, temp.path),
                      temp.path,
                      "pane.report_agent_session",
                      {
                          { "session_id", "default" },
                          { "server_epoch", epoch },
                          { "runtime_generation", generation },
                          { "pane_id",
                              restored_dynamic_pane_id },
                          { "agent_instance_id",
                              "persisted-agent" },
                          { "source", "draxul:codex" },
                          { "agent", "codex" },
                          { "integration_version", 1 },
                          { "sequence", sequence },
                          { "ref_kind", "id" },
                          { "ref_value",
                              "persisted-session-2" },
                      });
              };
        const auto old_epoch = report(
            "persistence-first", 1, 2);
        CHECK_FALSE(old_epoch.ok);
        CHECK(old_epoch.error_code == "server_replaced");

        const auto old_runtime = report(
            "persistence-second", 2, 2);
        CHECK_FALSE(old_runtime.ok);
        CHECK(old_runtime.error_code == "agent_replaced");

        const auto reported = report(
            "persistence-second", 1, 2);
        INFO(reported.error_code << ": "
                                 << reported.error_message);
        REQUIRE(reported.ok);
        REQUIRE(reported.result.contains("session_ref"));
        CHECK(reported.result["session_ref"]["value"]
            == "persisted-session-2");
        const auto stale = report(
            "persistence-second", 1, 2);
        CHECK_FALSE(stale.ok);
        CHECK(stale.error_code == "stale_report");

        REQUIRE(client.refresh(error));
        const auto& updated_panes
            = client.snapshot().spaces.front().tabs.front().panes;
        const auto updated = std::ranges::find(
            updated_panes, restored_dynamic_pane_id,
            &TopologyPane::pane_id);
        REQUIRE(updated != updated_panes.end());
        REQUIRE(updated->agent_session);
        CHECK(updated->agent_session->sequence == 2);
        CHECK(updated->agent_session->value
            == "persisted-session-2");
        run_guard.join();
    }

    auto updated = load_session_state_from_path(
        checkpoint, &load_error);
    INFO(load_error);
    REQUIRE(updated);
    const auto& updated_panes
        = updated->spaces.front().tabs.front().pane_layout.panes;
    const auto updated_dynamic = std::ranges::find(
        updated_panes, restored_dynamic_pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(updated_dynamic != updated_panes.end());
    REQUIRE(updated_dynamic->agent_session);
    CHECK(updated_dynamic->agent_session->sequence == 2);
    CHECK(updated_dynamic->agent_session->value
        == "persisted-session-2");
}

TEST_CASE("server periodically checkpoints topology without a UI",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-periodic-persistence");
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "periodic-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "periodic-writer",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "periodic-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Periodically Saved",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));

    std::optional<ServerStatusSnapshot> checkpoint_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        if (status.status->checkpoint_state == "ok")
        {
            checkpoint_status = status.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(checkpoint_status);
    REQUIRE(checkpoint_status->last_checkpoint_unix_ms != 0);
    REQUIRE(checkpoint_status->checkpoint_path
        == server_session_state_path(temp.path).string());

    auto saved = load_session_state_from_path(
        server_session_state_path(temp.path), &error);
    INFO(error);
    REQUIRE(saved);
    REQUIRE(saved->spaces.front().name
        == "Periodically Saved");
    run_guard.join();
}

TEST_CASE("server checkpoint writer does not block requests and shutdown captures the final revision",
    "[server][topology][persistence][concurrency]")
{
    TempDir temp("draxul-server-checkpoint-concurrency");
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool first_started = false;
        bool release_first = false;
        size_t calls = 0;
    };
    auto gate = std::make_shared<Gate>();
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval
        = std::chrono::milliseconds(5),
        .epoch_override = "checkpoint-concurrency",
        .checkpoint_shutdown_budget
        = std::chrono::seconds(2),
        .checkpoint_save
        = [gate](const SessionSnapshot& snapshot,
              const std::filesystem::path& path,
              std::string* error) {
              {
                  std::unique_lock lock(gate->mutex);
                  ++gate->calls;
                  if (gate->calls == 1)
                  {
                      gate->first_started = true;
                      gate->changed.notify_all();
                      gate->changed.wait(lock, [&] {
                          return gate->release_first;
                      });
                  }
              }
              return save_session_state_to_path(
                  snapshot, path, error);
          },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2), [&] {
                return gate->first_started;
            }));
    }

    const auto status_started
        = std::chrono::steady_clock::now();
    const auto status = ServerClient::status(
        temp.path, std::chrono::milliseconds(500));
    CHECK(std::chrono::steady_clock::now() - status_started
        < std::chrono::milliseconds(250));
    REQUIRE(status.ok);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "checkpoint-concurrency-ui",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "checkpoint-final-revision",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Final Revision",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));
    server.request_stop();
    {
        std::lock_guard lock(gate->mutex);
        gate->release_first = true;
    }
    gate->changed.notify_all();
    run_guard.join();

    CHECK(gate->calls >= 2);
    auto saved = load_session_state_from_path(
        server_session_state_path(temp.path), &error);
    INFO(error);
    REQUIRE(saved);
    CHECK(saved->spaces.front().name
        == "Final Revision");
}

TEST_CASE("server shutdown bounds a stalled checkpoint and the detached task remains safe",
    "[server][topology][persistence][concurrency]")
{
    TempDir temp("draxul-server-checkpoint-shutdown-budget");
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool started = false;
        bool release = false;
        bool finished = false;
    };
    auto gate = std::make_shared<Gate>();
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval
        = std::chrono::milliseconds(5),
        .epoch_override = "checkpoint-budget",
        .checkpoint_shutdown_budget
        = std::chrono::milliseconds(25),
        .checkpoint_save
        = [gate](const SessionSnapshot& snapshot,
              const std::filesystem::path& path,
              std::string* error) {
              {
                  std::unique_lock lock(gate->mutex);
                  gate->started = true;
                  gate->changed.notify_all();
                  gate->changed.wait(lock,
                      [&] { return gate->release; });
              }
              const bool saved = save_session_state_to_path(
                  snapshot, path, error);
              {
                  std::lock_guard lock(gate->mutex);
                  gate->finished = true;
              }
              gate->changed.notify_all();
              return saved;
          },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2),
            [&] { return gate->started; }));
    }
    const auto stop_started
        = std::chrono::steady_clock::now();
    run_guard.join();
    CHECK(std::chrono::steady_clock::now() - stop_started
        < std::chrono::milliseconds(250));

    {
        std::lock_guard lock(gate->mutex);
        gate->release = true;
    }
    gate->changed.notify_all();
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2),
            [&] { return gate->finished; }));
    }
    CHECK(std::filesystem::exists(
        server_session_state_path(temp.path)));
}

TEST_CASE("server reports checkpoint failure and preserves the last good file",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-persistence-failure");
    const auto checkpoint = server_session_state_path(temp.path);
    {
        ServerKernel seed({
            .runtime_directory = temp.path,
            .epoch_override = "failure-seed",
        });
        REQUIRE(seed.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard seed_guard(seed);
        seed_guard.join();
    }
    std::ifstream original_file(checkpoint, std::ios::binary);
    REQUIRE(original_file.is_open());
    const std::string original{
        std::istreambuf_iterator<char>(original_file),
        std::istreambuf_iterator<char>()
    };

    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "failure-test",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    std::filesystem::path blocked = checkpoint;
    blocked += ".tmp";
    REQUIRE(std::filesystem::create_directory(blocked));

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "failure-writer",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "failed-checkpoint-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Must Not Replace Last Good",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));

    std::optional<ServerStatusSnapshot> failed_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        if (status.status->checkpoint_state == "failed")
        {
            failed_status = status.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(failed_status);
    REQUIRE_FALSE(failed_status->checkpoint_error.empty());
    run_guard.join();

    std::ifstream preserved_file(checkpoint, std::ios::binary);
    REQUIRE(preserved_file.is_open());
    const std::string preserved{
        std::istreambuf_iterator<char>(preserved_file),
        std::istreambuf_iterator<char>()
    };
    REQUIRE(preserved == original);
}

TEST_CASE("server archives an unreadable checkpoint and resumes saving",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-invalid-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    REQUIRE(std::filesystem::create_directories(
        checkpoint.parent_path()));
    {
        std::ofstream invalid(checkpoint, std::ios::binary);
        invalid << "{broken";
    }
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "invalid-persistence",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->checkpoint_state != "disabled");
    REQUIRE_FALSE(status.status->restore_warnings.empty());
    std::vector<std::filesystem::path> archived;
    for (const auto& entry : std::filesystem::directory_iterator(
             checkpoint.parent_path()))
    {
        if (entry.path().filename().string().starts_with(
                "default.toml.corrupt-"))
        {
            archived.push_back(entry.path());
        }
    }
    REQUIRE(archived.size() == 1);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "invalid-reader",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    REQUIRE(client.snapshot().spaces.size() == 1);
    std::optional<ServerStatusSnapshot> saved_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto current = ServerClient::status(temp.path);
        REQUIRE(current.ok);
        if (current.status->checkpoint_state == "ok")
        {
            saved_status = current.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(saved_status);
    run_guard.join();

    std::ifstream retained(archived.front(), std::ios::binary);
    REQUIRE(retained.is_open());
    const std::string archived_text{
        std::istreambuf_iterator<char>(retained),
        std::istreambuf_iterator<char>()
    };
    REQUIRE(archived_text == "{broken");
    auto fresh = load_session_state_from_path(checkpoint, &error);
    INFO(error);
    REQUIRE(fresh);
}

TEST_CASE("server restores usable Spaces and checkpoints after partial restore",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-partial-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    {
        ServerKernel seed({
            .runtime_directory = temp.path,
            .epoch_override = "partial-seed",
        });
        REQUIRE(seed.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard seed_guard(seed);
        seed_guard.join();
    }
    std::string error;
    auto saved = load_session_state_from_path(
        checkpoint, &error);
    REQUIRE(saved);
    auto encoded = encode_session_state(*saved, &error);
    REQUIRE(encoded);
    auto cloned = decode_session_state(*encoded, &error);
    REQUIRE(cloned);
    SpaceSnapshot broken = std::move(cloned->spaces.front());
    broken.id = saved->next_space_id++;
    broken.name = "Broken Space";
    broken.tabs.front().pane_layout.panes.front().launch.remote_terminal_id.clear();
    saved->spaces.push_back(std::move(broken));
    REQUIRE(save_session_state_to_path(
        *saved, checkpoint, &error));
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "partial-restore",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->checkpoint_state
        == "restored_with_warnings");
    REQUIRE_FALSE(status.status->restore_warnings.empty());

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "partial-reader",
    });
    REQUIRE(client.refresh(error));
    REQUIRE(client.snapshot().spaces.size() == 1);
    REQUIRE(client.snapshot().spaces.front().name == "Space 1");
    TopologyCommand rename{
        .command_id = "partial-restore-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Recovered Space",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));
    bool checkpointed_after_warning = false;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto current = ServerClient::status(temp.path);
        REQUIRE(current.ok);
        if (current.status->checkpoint_state == "ok")
        {
            checkpointed_after_warning = true;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(checkpointed_after_warning);
    run_guard.join();

    auto recovered = load_session_state_from_path(
        checkpoint, &error);
    INFO(error);
    REQUIRE(recovered);
    REQUIRE(recovered->spaces.size() == 1);
    REQUIRE(recovered->spaces.front().name
        == "Recovered Space");
}

TEST_CASE("remote alternate screen preserves Unicode and resize semantics",
    "[server][remote-terminal][process][alternate-screen][unicode]")
{
    TempDir temp("draxul-real-remote-alternate");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client
        = remote_client(temp.path, "alternate-a", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
#ifdef _WIN32
    const std::string enter
        = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new(); [Console]::Write([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('G1s/MTA0OWhfX0FMVF/Ou1/nlYxfXw==')))\r";
    const std::string leave
        = "[Console]::Write([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('G1s/MTA0OWw=')))\r";
#else
    const std::string enter
        = "printf 'G1s/MTA0OWhfX0FMVF/Ou1/nlYxfXw==' | base64 -d\r";
    const std::string leave
        = "printf 'G1s/MTA0OWw=' | base64 -d\r";
#endif
    REQUIRE(client.send_input(enter, error));
    const bool saw_alternate_text
        = wait_for_text(client, "__ALT_\xCE\xBB_\xE7\x95\x8C__", error);
    INFO(error);
    INFO(snapshot_text(client.projection().snapshot()));
    REQUIRE(saw_alternate_text);
    const bool entered_alternate_screen
        = wait_for_alternate_screen(client, true, error);
#ifdef _WIN32
    if (!entered_alternate_screen && std::getenv("CI") != nullptr)
    {
        INFO(error);
        SKIP("Hosted Windows ConPTY does not expose alternate-screen mode transitions");
    }
#endif
    REQUIRE(entered_alternate_screen);
    REQUIRE(client.resize(52, 11, error));
    bool changed = false;
    REQUIRE(client.poll(changed, error));
    REQUIRE(client.projection().snapshot().cols == 52);
    REQUIRE(client.projection().snapshot().rows == 11);
    REQUIRE(client.projection().snapshot().metadata.modes.alternate_screen);

    REQUIRE(client.send_input(leave, error));
    REQUIRE(wait_for_alternate_screen(client, false, error));

    run_guard.join();
}

TEST_CASE("remote observer receives a burst of large resize events in bounded frames",
    "[server][remote-terminal]")
{
    TempDir temp("draxul-fake-remote-resize-burst");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto controller = remote_client(temp.path, "controller");
    auto observer = remote_client(temp.path, "observer");
    std::string error;
    REQUIRE(controller.attach(error));
    REQUIRE(observer.attach(error));
    bool changed = false;
    // Four alternating resizes are sufficient to force an observer through
    // multiple queued versions and prove convergence. Production-maximum
    // frame/queue limits are covered by the service-level tests above.
    for (int index = 0; index < 4; ++index)
    {
        const int cols = index % 2 == 0 ? 160 : 80;
        const int rows = index % 2 == 0 ? 36 : 24;
        REQUIRE(controller.resize(cols, rows, error));
        REQUIRE(controller.poll(changed, error));
        REQUIRE(changed);
    }

    for (int attempt = 0;
        attempt < 16
        && observer.projection().version()
            != controller.projection().version();
        ++attempt)
    {
        REQUIRE(observer.poll(changed, error));
        INFO(error);
        REQUIRE(changed);
    }
    REQUIRE(observer.projection().version()
        == controller.projection().version());
    REQUIRE(terminal_semantic_digest(observer.projection().snapshot())
        == terminal_semantic_digest(controller.projection().snapshot()));

    run_guard.join();
}

TEST_CASE("remote terminal projection rejects stale identity and sequence",
    "[client][remote-terminal]")
{
    TerminalSemanticSnapshot snapshot{
        .cols = 1,
        .rows = 1,
        .cells = { { .text = "A" } },
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "pane",
            .terminal_id = "terminal",
            .process_id = 42,
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = 5,
            },
            .process_id = 42,
            .snapshot = snapshot,
        },
    };
    RemoteTerminalProjection projection;
    std::string error;
    REQUIRE(projection.attach(attach, error));

    auto event = attach.state;
    event.kind = RemoteTerminalEventKind::Controller;
    event.snapshot.reset();
    event.version.sequence = 7;
    REQUIRE_FALSE(projection.apply(event, error));

    event.version.sequence = 6;
    event.version.server_epoch = "old-epoch";
    REQUIRE_FALSE(projection.apply(event, error));

    event.version.server_epoch = "epoch";
    event.version.generation = 2;
    REQUIRE_FALSE(projection.apply(event, error));

    event.version.generation = 1;
    event.version.sequence = 6;
    event.process_running = false;
    event.exit_code = 0;
    REQUIRE(projection.apply(event, error));
    CHECK_FALSE(projection.pane().process_running);
    CHECK(projection.pane().process_id == 0);
    REQUIRE(projection.pane().exit_code);
    CHECK(*projection.pane().exit_code == 0);
}

TEST_CASE("remote terminal compact codec validates coordinates and metadata",
    "[protocol][remote-terminal][validation]")
{
    TerminalDirtySnapshot delta{
        .cols = 2,
        .rows = 2,
        .full = true,
        .cells = {
            { .col = 0, .row = 0, .cell = { .text = "A" } },
            { .col = 1, .row = 0, .cell = { .text = "B" } },
            { .col = 0, .row = 1, .cell = { .text = "C" } },
            { .col = 1, .row = 1, .cell = { .text = "D" } },
        },
    };
    auto encoded = terminal_dirty_snapshot_to_json(delta);
    REQUIRE(encoded["attrs"].is_array());
    REQUIRE(encoded["links"].is_array());

    std::string error;
    auto duplicate = encoded;
    duplicate["cells"][1][0] = 0;
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        duplicate, error));

    auto too_many = encoded;
    too_many["full"] = false;
    too_many["cells"].push_back(too_many["cells"][0]);
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        too_many, error));

    auto bad_cursor = encoded;
    bad_cursor["metadata"]["cursor"]["col"] = 2;
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        bad_cursor, error));

    auto bad_mark = encoded;
    bad_mark["metadata"]["shell_marks"].push_back({
        { "kind", static_cast<int>(TerminalShellMarkKind::PromptStart) },
        { "row", 2 },
        { "exit_code", 0 },
    });
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        bad_mark, error));

    auto oversized_coordinate = encoded;
    oversized_coordinate["cells"][0][0]
        = std::numeric_limits<uint64_t>::max();
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        oversized_coordinate, error));

    auto expanded = encoded;
    expanded.erase("attrs");
    expanded.erase("links");
    expanded["cells"] = nlohmann::json::array({
        {
            { "col", 0 },
            { "row", 0 },
            { "cell", nlohmann::json::object() },
        },
    });
    REQUIRE_FALSE(terminal_dirty_snapshot_from_json(
        expanded, error));

    RemoteTerminalEvent event{
        .kind = RemoteTerminalEventKind::Controller,
        .version = {
            .server_epoch = "epoch",
            .terminal_id = "terminal",
            .generation = 1,
        },
        .controller_client_id = "controller",
    };
    auto bad_controller = remote_terminal_event_to_json(event);
    bad_controller["controller_client_id"]
        = "bad\x1b"
          "controller";
    REQUIRE_FALSE(remote_terminal_event_from_json(
        bad_controller, error));
}

TEST_CASE("remote terminal codec packs colours and deduplicates hyperlinks",
    "[protocol][remote-terminal][encoding]")
{
    HlAttr attr;
    attr.fg = Color(1.0f, 128.0f / 255.0f, 0.0f, 1.0f);
    attr.has_fg = true;
    TerminalSemanticSnapshot snapshot{
        .cols = 2,
        .rows = 1,
        .cells = {
            {
                .text = "A",
                .attr = attr,
                .hyperlink = "https://example.test/shared",
            },
            {
                .text = "B",
                .attr = attr,
                .hyperlink = "https://example.test/shared",
            },
        },
    };
    const auto encoded = terminal_semantic_snapshot_to_json(snapshot);
    REQUIRE(encoded["attrs"].size() == 1);
    REQUIRE(encoded["attrs"][0]["fg"].is_number_unsigned());
    REQUIRE(encoded["links"].size() == 1);
    REQUIRE(encoded["cells"][0][4] == encoded["cells"][1][4]);

    std::string error;
    const auto decoded
        = terminal_semantic_snapshot_from_json(encoded, error);
    REQUIRE(decoded);
    CHECK(*decoded == snapshot);
}

TEST_CASE("remote terminal scrollback advertised maximum round-trips",
    "[protocol][remote-terminal][scrollback]")
{
    TerminalSemanticSnapshot snapshot{
        .cols = 1,
        .rows = static_cast<int>(
            kRemoteTerminalMaxScrollbackPageRows),
        .cells = std::vector<TerminalCellSnapshot>(
            kRemoteTerminalMaxScrollbackPageRows,
            TerminalCellSnapshot{ .text = " " }),
    };
    RemoteTerminalScrollbackPage page{
        .version = {
            .server_epoch = "epoch",
            .terminal_id = "terminal",
            .generation = 1,
        },
        .total_rows = kRemoteTerminalMaxScrollbackPageRows,
        .offset_from_live = kRemoteTerminalMaxScrollbackPageRows,
        .cols = 1,
        .snapshot = snapshot,
    };
    std::string error;
    const auto decoded = remote_terminal_scrollback_page_from_json(
        remote_terminal_scrollback_page_to_json(page), error);
    REQUIRE(decoded);
    CHECK(*decoded == page);
}

TEST_CASE("remote terminal forwards OSC 52 clipboard writes without tracing content",
    "[server][remote-terminal][clipboard]")
{
    TempDir temp("draxul-remote-clipboard");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "clipboard-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client = remote_client(
        temp.path, "clipboard-client", "clipboard-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
#ifdef _WIN32
    const std::string command
        = "[Console]::Write([char]27 + ']52;c;cmVtb3RlIGNsaXBib2FyZA==' + [char]7)\r";
#else
    const std::string command
        = "printf '\\033]52;c;cmVtb3RlIGNsaXBib2FyZA==\\007'\r";
#endif
    REQUIRE(client.send_input(command, error));
    std::optional<std::string> clipboard;
    for (int attempt = 0; attempt < 300 && !clipboard; ++attempt)
    {
        bool changed = false;
        REQUIRE(client.poll(changed, error));
        clipboard = client.take_clipboard_write();
        if (!clipboard)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO("clipboard: "
        << (clipboard ? *clipboard : "<no clipboard event>"));
    REQUIRE(clipboard.has_value());
    REQUIRE(*clipboard == "remote clipboard");

    REQUIRE(client.suspend(error, 1));
#ifdef _WIN32
    const std::string hidden_command
        = "[Console]::Write([char]27 + ']52;c;aGlkZGVuIGNsaXBib2FyZA==' + [char]7)\r";
#else
    const std::string hidden_command
        = "printf '\\033]52;c;aGlkZGVuIGNsaXBib2FyZA==\\007'\r";
#endif
    REQUIRE(client.send_input(hidden_command, error, 2));
    bool suppressed = false;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        const auto metrics = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(metrics.ok);
        if (metrics.result["suppressed_clipboard_events"]
                .get<uint64_t>()
            > 0)
        {
            suppressed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(suppressed);
    REQUIRE(client.resume(error));
    CHECK_FALSE(client.take_clipboard_write().has_value());

    run_guard.join();
}

TEST_CASE("seeded remote terminal deltas converge or require snapshot resync",
    "[client][remote-terminal][delta][random]")
{
    constexpr int cols = 16;
    constexpr int rows = 8;
    TerminalSemanticSnapshot initial{
        .cols = cols,
        .rows = rows,
        .cells = std::vector<TerminalCellSnapshot>(
            static_cast<size_t>(cols) * rows),
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "pane",
            .terminal_id = "terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = 0,
            },
            .snapshot = initial,
        },
    };
    RemoteTerminalProjection complete;
    RemoteTerminalProjection interrupted;
    std::string error;
    REQUIRE(complete.attach(attach, error));
    REQUIRE(interrupted.attach(attach, error));

    std::mt19937 random(0xD4A5u);
    std::uniform_int_distribution<int> cell_count(1, 20);
    std::uniform_int_distribution<int> col_dist(0, cols - 1);
    std::uniform_int_distribution<int> row_dist(0, rows - 1);
    std::uniform_int_distribution<int> glyph_dist(0, 25);
    bool resync_requested = false;
    for (uint64_t sequence = 1; sequence <= 200; ++sequence)
    {
        TerminalDirtySnapshot delta{
            .cols = cols,
            .rows = rows,
        };
        for (int index = 0; index < cell_count(random); ++index)
        {
            delta.cells.push_back({
                .col = col_dist(random),
                .row = row_dist(random),
                .cell = {
                    .text = std::string(
                        1, static_cast<char>('A' + glyph_dist(random))),
                },
            });
        }
        RemoteTerminalEvent event{
            .kind = RemoteTerminalEventKind::Delta,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = sequence,
            },
            .delta = std::move(delta),
        };
        REQUIRE(complete.apply(event, error));

        if (sequence == 80)
            continue;
        if (sequence == 81)
        {
            REQUIRE_FALSE(interrupted.apply(event, error));
            resync_requested = true;
            RemoteTerminalEvent snapshot{
                .kind = RemoteTerminalEventKind::Snapshot,
                .version = event.version,
                .snapshot = complete.snapshot(),
            };
            REQUIRE(interrupted.apply(snapshot, error));
            continue;
        }
        REQUIRE(interrupted.apply(event, error));
        REQUIRE(terminal_semantic_digest(interrupted.snapshot())
            == terminal_semantic_digest(complete.snapshot()));
    }
    REQUIRE(resync_requested);
}

#ifdef DRAXUL_EXECUTABLE_PATH
#ifdef _WIN32
bool set_process_suspended(
    uint64_t process_id, bool suspend)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool changed = false;
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID
                != static_cast<DWORD>(process_id))
            {
                continue;
            }
            HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME, FALSE,
                entry.th32ThreadID);
            if (!thread)
                continue;
            const DWORD result = suspend
                ? SuspendThread(thread)
                : ResumeThread(thread);
            changed = changed || result != static_cast<DWORD>(-1);
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return changed;
}
#endif

TEST_CASE("ensure relaunches past a recycled PID and an expired startup marker",
    "[server][process][discovery][recovery]")
{
    const std::filesystem::path executable
        = DRAXUL_EXECUTABLE_PATH;

    auto ensure_and_stop
        = [&executable](const std::filesystem::path& runtime) {
              ServerEnsureOptions options{
                  .runtime_directory = runtime,
                  .executable_path = executable,
                  .client_id = "recovery-client",
                  .timeout = std::chrono::seconds(15),
              };
              const auto recovered
                  = ServerClient::ensure(options);
              INFO(recovered.error_message);
              REQUIRE(recovered.ready());
              std::string shutdown_error;
              REQUIRE(ServerClient::shutdown(runtime,
                  { .confirm_live_terminals = true },
                  shutdown_error));
              for (int attempt = 0;
                  attempt < 200
                  && std::filesystem::exists(
                      server_metadata_path(runtime));
                  ++attempt)
              {
                  std::this_thread::sleep_for(
                      std::chrono::milliseconds(10));
              }
              REQUIRE_FALSE(std::filesystem::exists(
                  server_metadata_path(runtime)));
          };

    TempDir recycled("draxul-server-recycled-pid");
    {
        std::ofstream output(
            server_metadata_path(recycled.path));
        output << nlohmann::json{
            { "version", kControlProtocolVersion },
            { "endpoint",
                R"(\\.\pipe\draxul-recycled-pid)" },
            { "token", std::string(64, 'a') },
            { "server_pid", test_process_id() },
            { "server_process_start_token",
                "a-different-process-incarnation" },
            { "published_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    ensure_and_stop(recycled.path);

    TempDir expired("draxul-server-expired-start");
    const auto marker = expired.path
        / ("server-starting-"
            + std::to_string(test_process_id())
            + ".json");
    {
        std::ofstream output(marker);
        output << nlohmann::json{
            { "pid", test_process_id() },
            { "process_start_token",
                test_process_start_token() },
            { "created_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    ensure_and_stop(expired.path);
    CHECK_FALSE(std::filesystem::exists(marker));
}

TEST_CASE("force stop uses published process identity when the server is unresponsive",
    "[server][process][discovery][recovery]")
{
    TempDir temp("draxul-server-force-stop-wedged");
    auto options = probe_options(temp.path);
    options.executable_path = DRAXUL_EXECUTABLE_PATH;
    options.launch_if_missing = true;
    options.timeout = std::chrono::seconds(15);
    const auto running = ServerClient::ensure(options);
    INFO(running.error_message);
    REQUIRE(running.ready());
    const uint64_t server_pid = running.welcome->server_pid;

#ifdef _WIN32
    REQUIRE(set_process_suspended(server_pid, true));
#else
    REQUIRE(::kill(static_cast<pid_t>(server_pid), SIGSTOP) == 0);
#endif
    std::string stop_error;
    const bool stopped = ServerClient::force_stop(
        temp.path, true, stop_error);
    if (!stopped)
    {
#ifdef _WIN32
        set_process_suspended(server_pid, false);
#else
        ::kill(static_cast<pid_t>(server_pid), SIGCONT);
#endif
    }
    INFO(stop_error);
    REQUIRE(stopped);

    bool alive = true;
    for (int attempt = 0; attempt < 200 && alive;
        ++attempt)
    {
#ifdef _WIN32
        HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, static_cast<DWORD>(server_pid));
        if (!process)
            alive = false;
        else
        {
            DWORD exit_code = STILL_ACTIVE;
            alive = GetExitCodeProcess(process, &exit_code)
                && exit_code == STILL_ACTIVE;
            CloseHandle(process);
        }
#else
        alive = ::kill(static_cast<pid_t>(server_pid), 0) == 0;
#endif
        if (alive)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }
    CHECK_FALSE(alive);
}

TEST_CASE("ten concurrent clients converge on one detached server epoch", "[server][process]")
{
    TempDir temp("draxul-server-process");
    const std::filesystem::path executable = DRAXUL_EXECUTABLE_PATH;

    std::vector<std::future<ServerProbeResult>> clients;
    for (int index = 0; index < 10; ++index)
    {
        clients.push_back(std::async(std::launch::async,
            [runtime = temp.path, executable, index] {
                return ServerClient::ensure({
                    .runtime_directory = runtime,
                    .executable_path = executable,
                    .client_id = "process-client-" + std::to_string(index),
                    .timeout = std::chrono::seconds(15),
                });
            }));
    }

    std::optional<ServerWelcome> identity;
    for (auto& client : clients)
    {
        const auto result = client.get();
        INFO(result.error_message);
        REQUIRE(result.ready());
        if (!identity)
            identity = result.welcome;
        REQUIRE(result.welcome->server_pid == identity->server_pid);
        REQUIRE(result.welcome->server_epoch == identity->server_epoch);
    }

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->connected_clients == 10);
    REQUIRE(status.status->terminals == 1);
    REQUIRE(status.status->sessions == 1);
    REQUIRE(status.status->spaces == 1);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    for (int attempt = 0;
        attempt < 200
        && std::filesystem::exists(server_metadata_path(temp.path));
        ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE_FALSE(std::filesystem::exists(server_metadata_path(temp.path)));
}
#endif

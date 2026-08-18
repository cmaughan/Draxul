#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

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

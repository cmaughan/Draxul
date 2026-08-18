#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

#include <draxul/async_frame_stream.h>

#include <tuple>

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

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
                "session-stream-v1")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "session-stream-commands-v1")
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
    CHECK(status.status->control_transport.listener_capacity == 4);
    CHECK(status.status->control_transport.accepted_connections
        > status.status->connected_clients);
    CHECK(status.status->control_transport.active_connections >= 1);
    CHECK(status.status->control_transport.peak_connections >= 1);
    CHECK(status.status->control_transport.requests >= 1);
    const auto status_method = std::ranges::find_if(
        status.status->control_transport.methods,
        [](const ServerControlMethodMetricsSnapshot& method) {
            return method.method == "server.status";
        });
    REQUIRE(status_method
        != status.status->control_transport.methods.end());
    CHECK(status_method->requests == 1);
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

TEST_CASE("server kernel keeps an authenticated Session stream alive past the idle lease",
    "[server][kernel][session-stream]")
{
    TempDir temp("draxul-server-stream-lifecycle");
    ServerKernel server({
        .runtime_directory = temp.path,
        .client_activity_timeout = std::chrono::milliseconds(300),
        .idle_wait_interval = std::chrono::milliseconds(20),
        .build_version = "unit-test",
        .epoch_override = "stream-lifecycle-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);

    const auto hello = ControlClient::request(control_id,
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "stream-lifecycle-client",
            .registration_nonce = "stream-lifecycle-registration",
            .capabilities = {
                std::string(kServerClientTokenCapability),
                "session-stream-v1",
                "session-stream-commands-v1",
            },
        }));
    REQUIRE(hello.ok);
    std::string parse_error;
    const auto welcome
        = server_welcome_from_json(hello.result, parse_error);
    INFO(parse_error);
    REQUIRE(welcome);
    REQUIRE(std::ranges::find(welcome->capabilities,
                "session-stream-v1")
        != welcome->capabilities.end());
    REQUIRE(std::ranges::find(welcome->capabilities,
                "session-stream-commands-v1")
        != welcome->capabilities.end());
    REQUIRE_FALSE(welcome->connection_token.empty());

    SessionStreamOpenRequest open_request{
        .server_epoch = welcome->server_epoch,
        .session_id = "default",
        .poll = {
            .request_serial = 1,
            .server_epoch = welcome->server_epoch,
        },
    };
    nlohmann::json open_params
        = session_stream_open_request_to_json(open_request);
    open_params["client_id"] = "stream-lifecycle-client";
    open_params["connection_token"]
        = welcome->connection_token;
    const auto opened = ControlClient::request(control_id,
        temp.path, "session.stream.open", std::move(open_params));
    REQUIRE(opened.ok);
    const auto stream_open = session_stream_open_response_from_json(
        opened.result, parse_error);
    INFO(parse_error);
    REQUIRE(stream_open);
    REQUIRE(stream_open->server_epoch == welcome->server_epoch);

    AsyncFrameStreamError stream_error;
    auto stream = AsyncFrameStreamClient::connect(
        stream_open->endpoint, std::chrono::seconds(2), stream_error);
    INFO(stream_error.code << ": " << stream_error.message);
    REQUIRE(stream);
    const SessionStreamClientFrame connect{
        .kind = SessionStreamClientFrameKind::Connect,
        .connect = SessionStreamConnectRequest{
            .server_epoch = welcome->server_epoch,
            .ticket = stream_open->ticket,
        },
    };
    REQUIRE(stream->write_frame(
        session_stream_client_frame_to_json(connect).dump(), {},
        stream_error));

    const auto read_frame = [&] {
        auto future = std::async(std::launch::async, [&] {
            std::string bytes;
            AsyncFrameStreamError error;
            const bool ok = stream->read_frame(bytes, {}, error);
            return std::tuple{
                ok, std::move(bytes),
                error.code + ": " + error.message,
            };
        });
        if (future.wait_for(std::chrono::seconds(2))
            != std::future_status::ready)
        {
            stream->close();
        }
        REQUIRE(future.wait_for(std::chrono::seconds(1))
            == std::future_status::ready);
        auto [ok, bytes, error] = future.get();
        INFO(error);
        REQUIRE(ok);
        auto frame = session_stream_server_frame_from_json(
            nlohmann::json::parse(bytes, nullptr, false), parse_error);
        INFO(parse_error);
        REQUIRE(frame);
        return *frame;
    };
    const auto initial = read_frame();
    REQUIRE(initial.kind == SessionStreamServerFrameKind::Events);
    REQUIRE(initial.events);
    CHECK(initial.events->request_serial == 1);

    REQUIRE(initial.events->topology.snapshot);
    TopologyCommand create_space{
        .client_id = "spoofed-client",
        .command_id = "stream-kernel-create-space",
        .expected_revision
            = initial.events->topology.snapshot->revision,
        .kind = TopologyCommandKind::CreateSpace,
        .name = "Stream-created Space",
    };
    nlohmann::json topology_params
        = topology_command_to_json(create_space);
    topology_params["session_id"] = "spoofed-session";
    const auto send_topology_command
        = [&](uint64_t request_id) {
              const SessionStreamClientFrame command_frame{
                  .kind = SessionStreamClientFrameKind::Command,
                  .command = SessionStreamCommand{
                      .request_id = request_id,
                      .server_epoch = welcome->server_epoch,
                      .method = "topology.command",
                      .params = topology_params,
                  },
              };
              return stream->write_frame(
                  session_stream_client_frame_to_json(
                      command_frame).dump(), {}, stream_error);
          };
    const auto read_command_result
        = [&](uint64_t request_id) {
              SessionStreamServerFrame response;
              for (int attempt = 0; attempt < 4; ++attempt)
              {
                  response = read_frame();
                  if (response.kind
                      == SessionStreamServerFrameKind::CommandResult)
                  {
                      break;
                  }
              }
              REQUIRE(response.kind
                  == SessionStreamServerFrameKind::CommandResult);
              REQUIRE(response.command_result);
              REQUIRE(response.command_result->request_id
                  == request_id);
              REQUIRE(response.command_result->ok);
              return *response.command_result;
          };
    REQUIRE(send_topology_command(77));
    const auto created_envelope = read_command_result(77);
    const auto created = topology_command_result_from_json(
        created_envelope.result, parse_error);
    INFO(parse_error);
    REQUIRE(created);
    REQUIRE(created->applied);
    CHECK_FALSE(created->duplicate);
    REQUIRE(created->snapshot.session_id == "default");
    REQUIRE(created->snapshot.spaces.size() == 2);

    // A distinct outer request reaches the kernel again. The repeated inner
    // command is duplicate only if the kernel replaced the spoofed identity
    // and Session with the authenticated stream binding both times.
    REQUIRE(send_topology_command(78));
    const auto duplicate_envelope = read_command_result(78);
    const auto duplicate = topology_command_result_from_json(
        duplicate_envelope.result, parse_error);
    INFO(parse_error);
    REQUIRE(duplicate);
    REQUIRE(duplicate->applied);
    REQUIRE(duplicate->duplicate);
    REQUIRE(duplicate->snapshot == created->snapshot);

    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const auto idle_status = ServerClient::status(temp.path);
    REQUIRE(idle_status.ok);
    CHECK(idle_status.status->connected_clients == 1);
    CHECK(stream->connected());

    stream->close();
    std::string disconnect_error;
    REQUIRE(ServerClient::disconnect(temp.path,
        "stream-lifecycle-client", disconnect_error,
        welcome->connection_token));
    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    run_guard.join();
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

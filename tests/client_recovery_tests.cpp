#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/agent_client.h>
#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/topology_client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace draxul;
using draxul::tests::TempDir;

namespace
{

TopologySnapshot topology_snapshot(uint64_t revision)
{
    return {
        .revision = revision,
        .session_id = "default",
        .spaces = { {
            .space_id = "space-1",
            .name = "Space 1",
            .tabs = { {
                .tab_id = "tab-1",
                .name = "Tab 1",
                .root_node_id = "node-1",
                .nodes = { {
                    .node_id = "node-1",
                    .is_leaf = true,
                    .pane_id = "pane-1",
                } },
                .panes = { {
                    .pane_id = "pane-1",
                    .name = "Pane 1",
                    .domain = TopologyPaneDomain::ClientLocal,
                    .client_host_kind = "nvim",
                } },
            } },
        } },
    };
}

} // namespace

TEST_CASE("client recovery backoff is bounded, jittered, and channel isolated",
    "[client][recovery][backoff]")
{
    const auto first_a
        = ClientRecoveryState::retry_delay_for(1, 1);
    const auto first_b
        = ClientRecoveryState::retry_delay_for(1, 40);
    CHECK(first_a != first_b);
    CHECK(first_a >= std::chrono::milliseconds(80));
    CHECK(first_b <= std::chrono::milliseconds(120));
    for (uint32_t attempt = 1; attempt <= 20; ++attempt)
    {
        CHECK(ClientRecoveryState::retry_delay_for(
                  attempt, UINT64_MAX)
            <= std::chrono::seconds(5));
    }

    ClientRecoveryState recovery("recovery-client");
    const auto first_terminal
        = recovery.note_failure("terminal:one");
    const auto second_terminal
        = recovery.note_failure("terminal:one");
    recovery.note_connected("terminal:two");
    CHECK(recovery.snapshot("terminal:one").attempts == 2);
    CHECK(recovery.snapshot("terminal:two").attempts == 0);
    CHECK(second_terminal > first_terminal);
    CHECK(is_resynchronizing_client_error(
        "invalid_connection_token"));
}

TEST_CASE("client recovery refresh atomically replaces server epoch and token",
    "[client][server][recovery][token]")
{
    TempDir temp("draxul-client-token-refresh");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    ClientRecoveryState recovery("token-client");
    REQUIRE(recovery.set_server_identity(
        "old-epoch", "old-token"));
    std::atomic<bool> previous_token_seen = false;
    std::atomic<bool> capability_seen = false;
    std::atomic<bool> registration_nonce_seen = false;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending(
                [&](const ControlRequest& request) {
                    if (request.method != "server.hello")
                    {
                        return ControlMethodResult::error(
                            "unknown_method",
                            "Unexpected test method.");
                    }
                    std::string parse_error;
                    const auto hello = server_hello_from_json(
                        request.params, parse_error);
                    if (!hello)
                    {
                        return ControlMethodResult::error(
                            "invalid_hello", parse_error);
                    }
                    previous_token_seen
                        = hello->connection_token == "old-token";
                    registration_nonce_seen
                        = hello->registration_nonce
                        == recovery.registration_nonce();
                    capability_seen = std::ranges::find(
                                          hello->capabilities,
                                          kServerClientTokenCapability)
                        != hello->capabilities.end();
                    return ControlMethodResult::success(
                        server_welcome_to_json({
                            .protocol_major = kServerProtocolMajor,
                            .protocol_minor = kServerProtocolMinor,
                            .server_pid = 42,
                            .server_epoch = "new-epoch",
                            .build_version = "test",
                            .connection_token = "new-token",
                            .capabilities = {
                                std::string(
                                    kServerClientTokenCapability),
                            },
                        }));
                });
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    });

    std::string error;
    REQUIRE(recovery.refresh_server_epoch(
        temp.path, "token-client", error));
    CHECK(previous_token_seen);
    CHECK(capability_seen);
    CHECK(registration_nonce_seen);
    CHECK((recovery.server_identity()
        == ClientServerIdentity{
            .server_epoch = "new-epoch",
            .connection_token = "new-token",
        }));
    CHECK_FALSE(recovery.set_server_epoch("new-epoch"));
    CHECK(recovery.server_identity().connection_token
        == "new-token");
    REQUIRE(recovery.set_server_epoch("later-epoch"));
    CHECK((recovery.server_identity()
        == ClientServerIdentity{
            .server_epoch = "later-epoch",
        }));

    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("projection clients refresh after a server revision rollback",
    "[client][server][recovery]")
{
    TempDir temp("draxul-client-revision-recovery");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    std::atomic<int> topology_snapshots = 0;
    std::atomic<int> agent_snapshots = 0;
    std::atomic<bool> topology_identity_seen = false;
    std::atomic<bool> agent_identity_seen = false;
    std::atomic<bool> topology_token_seen = false;
    std::atomic<bool> agent_token_seen = false;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending(
                [&](const ControlRequest& request) {
                    if (request.method == "topology.snapshot")
                    {
                        const int call = ++topology_snapshots;
                        return ControlMethodResult::success(
                            topology_snapshot_to_json(
                                topology_snapshot(
                                    call == 1 ? 9u : 1u)));
                    }
                    if (request.method == "topology.poll")
                    {
                        topology_identity_seen
                            = request.params.value(
                                  "client_id", "")
                            == "projection-client";
                        topology_token_seen
                            = request.params.value(
                                  "connection_token", "")
                            == "projection-token";
                        return ControlMethodResult::error(
                            "stale_topology_revision",
                            "Topology revision rolled back.");
                    }
                    if (request.method == "agent.snapshot")
                    {
                        const int call = ++agent_snapshots;
                        return ControlMethodResult::success(
                            server_agent_snapshot_to_json({
                                .revision = call == 1 ? 7u : 1u,
                                .session_id = "default",
                            }));
                    }
                    if (request.method == "agent.poll")
                    {
                        agent_identity_seen
                            = request.params.value(
                                  "client_id", "")
                            == "projection-client";
                        agent_token_seen
                            = request.params.value(
                                  "connection_token", "")
                            == "projection-token";
                        return ControlMethodResult::error(
                            "stale_agent_revision",
                            "Agent revision rolled back.");
                    }
                    return ControlMethodResult::error(
                        "unknown_method", "Unexpected test method.");
                });
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    });

    auto recovery = std::make_shared<ClientRecoveryState>(
        "projection-client");
    REQUIRE(recovery->set_server_identity(
        "projection-epoch", "projection-token"));
    TopologyClient topology({
        .runtime_directory = temp.path,
        .client_id = "projection-client",
        .recovery = recovery,
    });
    AgentClient agents({
        .runtime_directory = temp.path,
        .client_id = "projection-client",
        .recovery = recovery,
    });
    std::string error;
    REQUIRE(topology.refresh(error));
    REQUIRE(topology.snapshot().revision == 9);
    bool changed = false;
    REQUIRE(topology.poll(changed, error));
    CHECK(changed);
    CHECK(topology.snapshot().revision == 1);
    CHECK(topology.last_error_code().empty());

    REQUIRE(agents.refresh(error));
    REQUIRE(agents.snapshot().revision == 7);
    REQUIRE(agents.poll(changed, error));
    CHECK(changed);
    CHECK(agents.snapshot().revision == 1);
    CHECK(agents.last_error_code().empty());
    CHECK(topology_identity_seen);
    CHECK(agent_identity_seen);
    CHECK(topology_token_seen);
    CHECK(agent_token_seen);

    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("remote terminal client carries the shared recovery token",
    "[client][remote-terminal][recovery][token]")
{
    TempDir temp("draxul-terminal-token");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    const TerminalSemanticSnapshot snapshot{
        .cols = 1,
        .rows = 1,
        .cells = { { .text = " " } },
    };
    const RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "token-pane",
            .terminal_id = "token-terminal",
            .name = "Token terminal",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "terminal-epoch",
                .terminal_id = "token-terminal",
                .generation = 1,
            },
            .snapshot = snapshot,
        },
    };
    std::atomic<bool> token_seen = false;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending(
                [&](const ControlRequest& request) {
                    if (request.method == "fake.attach")
                    {
                        token_seen = request.params.value(
                                         "connection_token", "")
                            == "terminal-token";
                        return ControlMethodResult::success(
                            remote_terminal_attach_to_json(attach));
                    }
                    return ControlMethodResult::error(
                        "unknown_method",
                        "Unexpected test method.");
                });
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    });

    auto recovery = std::make_shared<ClientRecoveryState>(
        "terminal-client");
    REQUIRE(recovery->set_server_identity(
        "terminal-epoch", "terminal-token"));
    RemoteTerminalClient client({
        .runtime_directory = temp.path,
        .client_id = "terminal-client",
        .recovery = recovery,
    });
    std::string error;
    REQUIRE(client.attach(error));
    CHECK(token_seen);

    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

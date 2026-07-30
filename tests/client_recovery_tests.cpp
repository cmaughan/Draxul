#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/agent_client.h>
#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/server_client.h>
#include <draxul/topology_client.h>

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

    TopologyClient topology({
        .runtime_directory = temp.path,
        .client_id = "projection-client",
    });
    AgentClient agents({
        .runtime_directory = temp.path,
        .client_id = "projection-client",
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

    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

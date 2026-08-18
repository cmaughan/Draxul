#include <catch2/catch_all.hpp>

#include "support/control_test_support.h"

#include <draxul/control_plane.h>
#include <draxul/remote_session_client.h>
#include <draxul/server_client.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("remote Session client publishes topology and command results",
    "[control][client-worker]")
{
    const auto runtime = unique_control_runtime_directory();
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
                                .domain = TopologyPaneDomain::ClientLocal,
                                .client_host_kind = "platform_default",
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
    const auto runtime = unique_control_runtime_directory();
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
                                .domain = TopologyPaneDomain::ClientLocal,
                                .client_host_kind = "platform_default",
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

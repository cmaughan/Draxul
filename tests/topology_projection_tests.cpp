#include <catch2/catch_all.hpp>

#include "../libs/draxul-server/src/session_topology_bridge.h"

#include <draxul/topology_projection.h>

using namespace draxul;

namespace
{

TopologyTab split_tab()
{
    return {
        .tab_id = "tab-1",
        .name = "Work",
        .root_node_id = "root",
        .nodes = {
            {
                .node_id = "root",
                .is_leaf = false,
                .direction
                = TopologySplitDirection::Vertical,
                .ratio = 0.6f,
                .first_node_id = "left",
                .second_node_id = "right",
            },
            {
                .node_id = "left",
                .is_leaf = true,
                .pane_id = "pane-shell",
            },
            {
                .node_id = "right",
                .is_leaf = true,
                .pane_id = "pane-future",
            },
        },
        .panes = {
            {
                .pane_id = "pane-shell",
                .name = "Shell",
                .domain = TopologyPaneDomain::ServerTerminal,
                .terminal_id = "terminal-1",
            },
            {
                .pane_id = "pane-future",
                .name = "Future",
                .domain = TopologyPaneDomain::ClientLocal,
                .client_host_kind = "future-view",
                .client_working_directory = "D:/future",
                .client_source_path = "cards/next.md",
                .companion_owner_pane_id = "pane-shell",
            },
        },
    };
}

} // namespace

TEST_CASE("topology projection owns bidirectional Space and tab identities",
    "[client][topology][projection]")
{
    TopologyProjection projection;
    CHECK(projection.empty());
    projection.bind_space("space-a", 7);
    projection.bind_tab("tab-a", 7, 9);
    CHECK(projection.local_space("space-a") == 7);
    CHECK(projection.remote_space(7) == "space-a");
    CHECK(projection.local_tab("tab-a")
        == std::pair<SpaceId, int>{ 7, 9 });

    projection.bind_space("space-b", 7);
    CHECK_FALSE(projection.local_space("space-a"));
    CHECK(projection.remote_space(7) == "space-b");
    projection.erase_space("space-b");
    CHECK(projection.empty());
}

TEST_CASE("topology projection builds stable renderer-free pane layouts",
    "[client][topology][projection]")
{
    TopologyProjection projection;
    std::string error;
    const TopologyTab remote = split_tab();
    auto first = projection.project_tab(
        remote, kInvalidLeaf, HostKind::PowerShell, error);
    REQUIRE(first);
    CHECK(error.empty());
    CHECK(first->requires_reconcile);
    REQUIRE(first->layout.tree.root);
    CHECK_FALSE(first->layout.tree.root->is_leaf);
    CHECK(first->layout.tree.root->ratio
        == Catch::Approx(0.6f));
    REQUIRE(first->layout.panes.size() == 2);
    CHECK(first->layout.panes[0].launch.kind
        == HostKind::RemoteTerminal);
    CHECK(first->layout.panes[0].launch.remote_terminal_id
        == "terminal-1");
    CHECK(first->layout.panes[1].launch.kind
        == HostKind::PowerShell);
    CHECK(first->layout.panes[1].launch.client_host_kind
        == "future-view");
    CHECK(first->layout.panes[1].launch.working_dir
        == "D:/future");
    CHECK(first->layout.panes[1].launch.source_path
        == "cards/next.md");
    CHECK(first->layout.panes[1].launch.companion_owner_pane_id
        == "pane-shell");
    const LeafId shell_leaf
        = first->layout.panes[0].leaf_id;
    const LeafId future_leaf
        = first->layout.panes[1].leaf_id;
    CHECK(shell_leaf != future_leaf);
    CHECK(first->layout.tree.focused_id == shell_leaf);

    projection.commit_tab(remote.tab_id, *first);
    CHECK(projection.divider_node("tab-1", 0)
        == "root");
    auto unchanged = projection.project_tab(
        remote, future_leaf, HostKind::PowerShell, error);
    REQUIRE(unchanged);
    CHECK_FALSE(unchanged->requires_reconcile);
    CHECK(unchanged->pane_names
        == (std::vector<std::pair<LeafId, std::string>>{
            { shell_leaf, "Shell" },
            { future_leaf, "Future" },
        }));

    projection.prune_panes({ "pane-future" });
    CHECK_FALSE(projection.local_pane("pane-shell"));
    CHECK(projection.local_pane("pane-future")
        == future_leaf);
}

TEST_CASE("topology projection rejects invalid split graphs without an App",
    "[client][topology][projection]")
{
    TopologyProjection projection;
    TopologyTab invalid = split_tab();
    invalid.nodes[0].second_node_id = "root";
    std::string error;
    CHECK_FALSE(projection.project_tab(
        invalid, kInvalidLeaf, HostKind::Zsh, error));
    CHECK(error
        == "Topology contains a missing or cyclic split node.");
}

TEST_CASE("topology projection preserves plugin launch identity and config",
    "[client][topology][projection][plugin]")
{
    TopologyProjection projection;
    TopologyTab remote = split_tab();
    remote.panes[1].client_host_kind = "plugin";
    remote.panes[1].client_plugin_id
        = "dev.draxul.spinning-triangle";
    remote.panes[1].client_plugin_config_json
        = R"({"paused":true,"initial_angle":0.5})";
    std::string error;
    const auto projected = projection.project_tab(
        remote, kInvalidLeaf, HostKind::PowerShell, error);
    REQUIRE(projected);
    REQUIRE(projected->layout.panes.size() == 2);
    const auto& launch = projected->layout.panes[1].launch;
    CHECK(launch.kind == HostKind::Plugin);
    CHECK(launch.client_plugin_id
        == "dev.draxul.spinning-triangle");
    CHECK(launch.client_plugin_config_json
        == R"({"paused":true,"initial_angle":0.5})");
}

TEST_CASE("topology projection releases command activations in revision order",
    "[client][topology][projection][command]")
{
    TopologyProjection projection;
    projection.remember_command_activation(
        { .kind = TopologyCommandKind::CreateTab },
        "tab-created", 4);
    projection.remember_command_activation(
        { .kind = TopologyCommandKind::SplitPane },
        "pane-created", 5);

    auto first = projection.take_ready_command_activations(4);
    REQUIRE(first.size() == 1);
    CHECK(first.front().command.kind
        == TopologyCommandKind::CreateTab);
    CHECK(first.front().created_id == "tab-created");
    auto second = projection.take_ready_command_activations(5);
    REQUIRE(second.size() == 1);
    CHECK(second.front().created_id == "pane-created");
}

TEST_CASE("agent identity survives server capture and client projection identically",
    "[client][server][topology][projection][agent]")
{
    // Acceptance test for the historical bug where the client projection
    // dropped agent/agent_session/restore_policy while the server capture
    // preserved them. Both directions now run the shared
    // topology_tab_to_layout conversion, so a pane that owns an agent must
    // arrive with identical agent identity through either path.
    TopologyTab remote = split_tab();
    remote.panes[1].agent = AgentIdentity{
        .profile_id = "claude",
        .kind = "claude",
        .display_name = "Claude",
        .instance_id = "agent-default-7",
    };
    remote.panes[1].agent_session = AgentSessionRef{
        .source = "draxul:claude",
        .agent_kind = "claude",
        .integration_version = 2,
        .sequence = 41,
        .kind = AgentSessionRefKind::Id,
        .value = "session-1234",
    };
    remote.panes[1].restore_policy = AgentRestorePolicy::Fresh;

    // Server path: topology -> durable session snapshot.
    const TopologySnapshot topology{
        .revision = 3,
        .session_id = "default",
        .spaces = { TopologySpace{
            .space_id = "space-1",
            .name = "Space 1",
            .tabs = { remote },
        } },
    };
    std::string error;
    const auto captured = capture_session_topology(topology, error);
    INFO(error);
    REQUIRE(captured);
    REQUIRE(captured->spaces.size() == 1);
    REQUIRE(captured->spaces[0].tabs.size() == 1);
    const auto& captured_panes
        = captured->spaces[0].tabs[0].pane_layout.panes;
    REQUIRE(captured_panes.size() == 2);

    // Client path: topology -> projected pane layout.
    TopologyProjection projection;
    const auto projected = projection.project_tab(
        remote, kInvalidLeaf, HostKind::Zsh, error);
    INFO(error);
    REQUIRE(projected);
    REQUIRE(projected->layout.panes.size() == 2);

    for (const SessionPaneSnapshot* pane :
        { &captured_panes[1], &projected->layout.panes[1] })
    {
        CHECK(pane->pane_id == "pane-future");
        REQUIRE(pane->agent);
        CHECK(*pane->agent == *remote.panes[1].agent);
        REQUIRE(pane->agent_session);
        CHECK(*pane->agent_session == *remote.panes[1].agent_session);
        CHECK(pane->restore_policy == AgentRestorePolicy::Fresh);
    }
    for (const SessionPaneSnapshot* pane :
        { &captured_panes[0], &projected->layout.panes[0] })
    {
        CHECK(pane->pane_id == "pane-shell");
        CHECK_FALSE(pane->agent);
        CHECK_FALSE(pane->agent_session);
        CHECK(pane->launch.kind == HostKind::RemoteTerminal);
        CHECK(pane->launch.remote_terminal_id == "terminal-1");
    }
}

TEST_CASE("server shell ownership classifier covers every HostKind",
    "[host_kind][topology][projection]")
{
    static_assert(kAllHostKinds.size()
        == static_cast<size_t>(HostKind::Count));
    for (const HostKind kind : kAllHostKinds)
    {
        const bool expected = kind == HostKind::PowerShell
            || kind == HostKind::Bash
            || kind == HostKind::Zsh
            || kind == HostKind::Wsl
            || kind == HostKind::RemoteTerminal;
        CHECK(is_server_owned_shell_host(kind)
            == expected);
    }
}

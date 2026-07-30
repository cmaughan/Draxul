#include <catch2/catch_all.hpp>

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
                .domain
                = TopologyPaneDomain::ServerTerminal,
                .terminal_id = "terminal-1",
            },
            {
                .pane_id = "pane-future",
                .name = "Future",
                .domain
                = TopologyPaneDomain::ClientLocal,
                .client_host_kind = "future-view",
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
        == "Server tab contains an invalid split tree.");
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

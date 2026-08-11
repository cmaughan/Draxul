#include <catch2/catch_all.hpp>

#include "topology_mutation_route.h"

using namespace draxul;

namespace
{

ServerTopologyMutationRoute::Deps server_deps(
    std::vector<TopologyCommand>& commands,
    TopologyPaneDomain pane_domain
    = TopologyPaneDomain::ServerTerminal)
{
    return {
        .resolve_space = [](SpaceId id)
            -> std::optional<std::string> {
            return id == 7
                ? std::optional<std::string>("space-7")
                : std::nullopt;
        },
        .resolve_tab = [](SpaceId space_id, int tab_id)
            -> std::optional<std::string> {
            return space_id == 7 && tab_id == 9
                ? std::optional<std::string>("tab-9")
                : std::nullopt;
        },
        .resolve_pane = [](SpaceId space_id, int tab_id,
                            LeafId leaf)
            -> std::optional<std::string> {
            if (space_id != 7 || tab_id != 9)
                return std::nullopt;
            if (leaf == 3)
                return "pane-3";
            if (leaf == 4)
                return "pane-4";
            return std::nullopt;
        },
        .resolve_divider = [](
                                std::string_view tab_id,
                                DividerId divider)
            -> std::optional<std::string> {
            return tab_id == "tab-9" && divider == 2
                ? std::optional<std::string>("node-2")
                : std::nullopt;
        },
        .resolve_pane_domain
        = [pane_domain](SpaceId, int, LeafId) {
              return std::optional(pane_domain);
          },
        .enqueue = [&commands](
                       TopologyCommand command,
                       std::string&) {
            commands.push_back(std::move(command));
            return true;
        },
        .client_plugin_panes_supported = true,
        .platform_default_host_kind = HostKind::PowerShell,
    };
}

TopologyMutation targeted(TopologyMutationKind kind)
{
    return {
        .kind = kind,
        .space_id = 7,
        .tab_id = 9,
        .pane_id = 3,
        .target_pane_id = 4,
        .divider_id = 2,
        .name = "Renamed",
        .root_directory
            = std::filesystem::path("D:/work"),
        .direction = TopologySplitDirection::Horizontal,
        .ratio = 0.7f,
        .ratio_delta = 0.05f,
        .move_delta = 1,
        .pixel_width = 1200,
        .pixel_height = 800,
    };
}

} // namespace

TEST_CASE("topology mutation factory selects exactly one route",
    "[app][topology][mutation_route]")
{
    int local_calls = 0;
    std::vector<TopologyCommand> commands;
    auto local = make_topology_mutation_route(
        false,
        [&](const TopologyMutation&) {
            ++local_calls;
            return TopologyMutationResult::applied();
        },
        server_deps(commands));
    REQUIRE(local);
    CHECK(local->route_kind()
        == TopologyMutationRouteKind::Local);
    CHECK(local->mutate(
              targeted(TopologyMutationKind::RenameSpace))
              .applied_locally());
    CHECK(local_calls == 1);
    CHECK(commands.empty());

    auto server = make_topology_mutation_route(
        true,
        [&](const TopologyMutation&) {
            ++local_calls;
            return TopologyMutationResult::applied();
        },
        server_deps(commands));
    REQUIRE(server);
    CHECK(server->route_kind()
        == TopologyMutationRouteKind::ServerBacked);
    const auto result = server->mutate(
        targeted(TopologyMutationKind::RenameSpace));
    CHECK(result.state
        == TopologyMutationState::Enqueued);
    CHECK(local_calls == 1);
    REQUIRE(commands.size() == 1);
    CHECK(commands.back().kind
        == TopologyCommandKind::RenameSpace);
}

TEST_CASE("server topology route covers every local mutation family",
    "[app][topology][mutation_route][parity]")
{
    std::vector<TopologyCommand> commands;
    ServerTopologyMutationRoute route(server_deps(commands));
    const std::vector<std::pair<TopologyMutationKind,
        TopologyCommandKind>>
        cases = {
            { TopologyMutationKind::CreateSpace,
                TopologyCommandKind::CreateSpace },
            { TopologyMutationKind::RenameSpace,
                TopologyCommandKind::RenameSpace },
            { TopologyMutationKind::CloseSpace,
                TopologyCommandKind::CloseSpace },
            { TopologyMutationKind::CreateTab,
                TopologyCommandKind::CreateTab },
            { TopologyMutationKind::RenameTab,
                TopologyCommandKind::RenameTab },
            { TopologyMutationKind::CloseTab,
                TopologyCommandKind::CloseTab },
            { TopologyMutationKind::MoveTab,
                TopologyCommandKind::MoveTab },
            { TopologyMutationKind::SplitPane,
                TopologyCommandKind::SplitPane },
            { TopologyMutationKind::UpdateClientPane,
                TopologyCommandKind::UpdateClientPane },
            { TopologyMutationKind::DuplicatePane,
                TopologyCommandKind::SplitPane },
            { TopologyMutationKind::ClosePane,
                TopologyCommandKind::ClosePane },
            { TopologyMutationKind::RenamePane,
                TopologyCommandKind::RenamePane },
            { TopologyMutationKind::SwapPane,
                TopologyCommandKind::SwapPane },
            { TopologyMutationKind::RestartPane,
                TopologyCommandKind::RestartPane },
            { TopologyMutationKind::SetSplitRatio,
                TopologyCommandKind::SetSplitRatio },
            { TopologyMutationKind::EqualizeSplits,
                TopologyCommandKind::EqualizeSplits },
        };

    for (const auto& [local_kind, protocol_kind] : cases)
    {
        INFO(static_cast<int>(local_kind));
        commands.clear();
        const auto result = route.mutate(targeted(local_kind));
        REQUIRE(result.state
            == TopologyMutationState::Enqueued);
        REQUIRE(commands.size() == 1);
        CHECK(commands.front().kind == protocol_kind);
    }

    commands.clear();
    TopologyMutation client_split
        = targeted(TopologyMutationKind::SplitPane);
    client_split.host_kind = HostKind::Nvim;
    REQUIRE(route.mutate(client_split).accepted());
    REQUIRE(commands.size() == 1);
    CHECK(commands.front().pane_domain
        == TopologyPaneDomain::ClientLocal);
    CHECK(commands.front().client_host_kind == "nvim");

    commands.clear();
    TopologyMutation shell_split
        = targeted(TopologyMutationKind::SplitPane);
    shell_split.host_kind.reset();
    REQUIRE(route.mutate(shell_split).accepted());
    REQUIRE(commands.size() == 1);
    CHECK(commands.front().pane_domain
        == TopologyPaneDomain::ServerTerminal);
}

TEST_CASE("server topology route preserves client-local launch descriptors",
    "[app][topology][mutation_route][client_local]")
{
    std::vector<TopologyCommand> commands;
    ServerTopologyMutationRoute route(server_deps(commands));
    TopologyMutation preview
        = targeted(TopologyMutationKind::SplitPane);
    preview.host_kind = HostKind::Markdown;
    preview.working_directory = "D:/dev/Draxul";
    preview.source_path = "kanban/pending/card.md";
    preview.companion_pane = true;
    preview.ratio = 2.0f / 3.0f;

    REQUIRE(route.mutate(preview).accepted());
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].client_host_kind == "markdown");
    CHECK(commands[0].client_working_directory
        == "D:/dev/Draxul");
    CHECK(commands[0].client_source_path
        == "kanban/pending/card.md");
    CHECK(commands[0].companion_owner_pane_id == "pane-3");
    CHECK(commands[0].ratio == Catch::Approx(2.0f / 3.0f));
}

TEST_CASE("server topology route requires plugin capability before enqueue",
    "[app][topology][mutation_route][plugin]")
{
    std::vector<TopologyCommand> commands;
    auto deps = server_deps(commands);
    deps.client_plugin_panes_supported = false;
    ServerTopologyMutationRoute route(std::move(deps));
    TopologyMutation plugin
        = targeted(TopologyMutationKind::SplitPane);
    plugin.host_kind = HostKind::Plugin;
    plugin.plugin_id = "dev.draxul.spinning-triangle";
    plugin.plugin_config_json = R"({"paused":false})";

    const auto result = route.mutate(plugin);
    CHECK_FALSE(result.accepted());
    CHECK(result.error.find("restart the server")
        != std::string::npos);
    CHECK(commands.empty());

    deps = server_deps(commands);
    ServerTopologyMutationRoute supported(std::move(deps));
    REQUIRE(supported.mutate(plugin).accepted());
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].client_host_kind == "plugin");
    CHECK(commands[0].client_plugin_id
        == "dev.draxul.spinning-triangle");
    CHECK(commands[0].client_plugin_config_json
        == R"({"paused":false})");
}

TEST_CASE("server route keeps client-local restart on the client",
    "[app][topology][mutation_route][parity]")
{
    int local_calls = 0;
    std::vector<TopologyCommand> commands;
    auto deps = server_deps(
        commands, TopologyPaneDomain::ClientLocal);
    deps.apply_client_local
        = [&](const TopologyMutation& mutation) {
              ++local_calls;
              CHECK(mutation.kind
                  == TopologyMutationKind::RestartPane);
              return TopologyMutationResult::applied();
          };
    ServerTopologyMutationRoute route(std::move(deps));

    const auto result = route.mutate(
        targeted(TopologyMutationKind::RestartPane));
    CHECK(result.applied_locally());
    CHECK(local_calls == 1);
    CHECK(commands.empty());
}

TEST_CASE("server topology route rejects unresolved local identities",
    "[app][topology][mutation_route]")
{
    std::vector<TopologyCommand> commands;
    ServerTopologyMutationRoute route(server_deps(commands));
    TopologyMutation mutation
        = targeted(TopologyMutationKind::ClosePane);
    mutation.pane_id = 99;

    const auto result = route.mutate(mutation);
    CHECK_FALSE(result.accepted());
    CHECK(result.error
        == "Shared focused pane could not be resolved.");
    CHECK(commands.empty());
}

#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

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

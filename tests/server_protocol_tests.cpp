#include <catch2/catch_test_macros.hpp>

#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>
#include <draxul/topology_protocol.h>

#include <nlohmann/json.hpp>

using namespace draxul;

TEST_CASE("server protocol round-trips hello welcome and status", "[server][protocol]")
{
    const ServerHello hello{
        .client_id = "ui-test",
        .capabilities = { "status", "graceful-shutdown" },
    };
    std::string error;
    const auto decoded_hello = server_hello_from_json(
        server_hello_to_json(hello), error);
    REQUIRE(decoded_hello == hello);

    const ServerWelcome welcome{
        .protocol_major = 1,
        .protocol_minor = 0,
        .server_pid = 42,
        .server_epoch = "epoch",
        .build_version = "test",
        .capabilities = { "status" },
    };
    const auto decoded_welcome = server_welcome_from_json(
        server_welcome_to_json(welcome), error);
    REQUIRE(decoded_welcome == welcome);

    const ServerStatusSnapshot status{
        .state = "ready",
        .protocol_major = 1,
        .protocol_minor = 0,
        .server_pid = 42,
        .server_epoch = "epoch",
        .build_version = "test",
        .uptime_ms = 123,
        .connected_clients = 2,
        .checkpoint_path = "sessions/default.toml",
        .checkpoint_state = "ok",
        .last_checkpoint_unix_ms = 456,
        .restore_warnings = { "one pane was skipped" },
        .session_statuses = {
            {
                .session_id = "default",
                .session_name = "Daily",
                .spaces = 2,
                .terminals = 3,
                .live_terminals = 1,
                .checkpoint_path = "sessions/default.toml",
                .checkpoint_state = "ok",
                .last_checkpoint_unix_ms = 456,
            },
        },
    };
    const auto decoded_status = server_status_from_json(
        server_status_to_json(status), error);
    REQUIRE(decoded_status == status);
}

TEST_CASE("server protocol rejects malformed identity and capabilities", "[server][protocol]")
{
    std::string error;
    auto hello_json = server_hello_to_json({
        .client_id = "",
    });
    REQUIRE_FALSE(server_hello_from_json(hello_json, error));

    auto welcome_json = server_welcome_to_json({
        .protocol_major = 1,
        .server_pid = 1,
        .server_epoch = "epoch",
        .capabilities = { "status", "status" },
    });
    REQUIRE_FALSE(server_welcome_from_json(welcome_json, error));
}

TEST_CASE("server probe states have stable diagnostic names", "[server][protocol]")
{
    REQUIRE(to_string(ServerProbeState::Absent) == "absent");
    REQUIRE(to_string(ServerProbeState::Starting) == "starting");
    REQUIRE(to_string(ServerProbeState::Ready) == "ready");
    REQUIRE(to_string(ServerProbeState::Busy) == "busy");
    REQUIRE(to_string(ServerProbeState::Incompatible) == "incompatible");
    REQUIRE(to_string(ServerProbeState::Crashed) == "crashed");
    REQUIRE(to_string(ServerProbeState::Stale) == "stale");
    REQUIRE(to_string(ServerProbeState::LaunchFailed) == "launch_failed");
}

TEST_CASE("remote terminal protocol round-trips snapshots and deltas",
    "[server][protocol][remote-terminal]")
{
    TerminalSemanticSnapshot snapshot{
        .cols = 2,
        .rows = 1,
        .cells = {
            { .text = "A", .attr = { .bold = true } },
            { .text = "B", .hyperlink = "https://draxul.dev" },
        },
        .metadata = {
            .cursor = { .col = 1, .shape = CursorShape::Vertical },
            .title = "remote",
            .working_directory = "/tmp",
        },
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "pane",
            .terminal_id = "terminal",
            .name = "Fake Remote",
            .execution_domain = "server_terminal",
            .process_id = 42,
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 2,
                .sequence = 7,
            },
            .process_id = 42,
            .controller_client_id = "client-a",
            .snapshot = snapshot,
        },
    };

    std::string error;
    const auto decoded_attach = remote_terminal_attach_from_json(
        remote_terminal_attach_to_json(attach), error);
    INFO(error);
    REQUIRE(decoded_attach == attach);

    RemoteTerminalEvent delta{
        .kind = RemoteTerminalEventKind::Delta,
        .version = {
            .server_epoch = "epoch",
            .terminal_id = "terminal",
            .generation = 2,
            .sequence = 8,
        },
        .controller_client_id = "client-a",
        .delta = TerminalDirtySnapshot{
            .cols = 2,
            .rows = 1,
            .cells = {
                { .col = 1, .row = 0, .cell = { .text = "C" } },
            },
            .metadata = snapshot.metadata,
        },
    };
    const auto decoded_delta = remote_terminal_event_from_json(
        remote_terminal_event_to_json(delta), error);
    INFO(error);
    REQUIRE(decoded_delta == delta);

    RemoteTerminalEvent clipboard{
        .kind = RemoteTerminalEventKind::Clipboard,
        .version = {
            .server_epoch = "epoch",
            .terminal_id = "terminal",
            .generation = 2,
            .sequence = 9,
        },
        .controller_client_id = "client-a",
        .clipboard = "remote clipboard",
    };
    const auto decoded_clipboard = remote_terminal_event_from_json(
        remote_terminal_event_to_json(clipboard), error);
    INFO(error);
    REQUIRE(decoded_clipboard == clipboard);
}

TEST_CASE("remote terminal protocol rejects incomplete full snapshots",
    "[server][protocol][remote-terminal]")
{
    TerminalSemanticSnapshot snapshot{
        .cols = 2,
        .rows = 2,
        .cells = { { .text = "only-one" } },
    };
    std::string error;
    REQUIRE_FALSE(terminal_semantic_snapshot_from_json(
        terminal_semantic_snapshot_to_json(snapshot), error));
}

TEST_CASE("remote terminal protocol round-trips a scrollback page",
    "[server][protocol][remote-terminal][scrollback]")
{
    RemoteTerminalScrollbackPage page{
        .version = {
            .server_epoch = "epoch",
            .terminal_id = "terminal",
            .generation = 3,
            .sequence = 9,
        },
        .total_rows = 42,
        .offset_from_live = 2,
        .cols = 2,
        .snapshot = TerminalSemanticSnapshot{
            .cols = 2,
            .rows = 2,
            .cells = {
                { .text = "A" },
                { .text = "B" },
                { .text = "C" },
                { .text = "D" },
            },
        },
    };
    std::string error;
    const auto decoded = remote_terminal_scrollback_page_from_json(
        remote_terminal_scrollback_page_to_json(page), error);
    INFO(error);
    REQUIRE(decoded == page);
}

TEST_CASE("topology protocol round-trips neutral split and pane values",
    "[server][protocol][topology]")
{
    TopologySnapshot snapshot{
        .revision = 7,
        .session_id = "default",
        .spaces = {
            {
                .space_id = "space-1",
                .name = "Draxul",
                .root_directory = "D:/dev/Draxul",
                .tabs = {
                    {
                        .tab_id = "tab-1",
                        .name = "Shells",
                        .name_user_set = false,
                        .root_node_id = "split-1",
                        .nodes = {
                            {
                                .node_id = "split-1",
                                .is_leaf = false,
                                .direction = TopologySplitDirection::Vertical,
                                .ratio = 0.6f,
                                .first_node_id = "leaf-1",
                                .second_node_id = "leaf-2",
                            },
                            {
                                .node_id = "leaf-1",
                                .pane_id = "pane-1",
                            },
                            {
                                .node_id = "leaf-2",
                                .pane_id = "pane-2",
                            },
                        },
                        .panes = {
                            {
                                .pane_id = "pane-1",
                                .name = "Server",
                                .domain = TopologyPaneDomain::ServerTerminal,
                                .terminal_id = "terminal-1",
                            },
                            {
                                .pane_id = "pane-2",
                                .name = "Editor",
                                .domain = TopologyPaneDomain::ClientLocal,
                                .client_host_kind = "nvim",
                            },
                        },
                    },
                },
            },
        },
    };
    std::string error;
    const auto decoded = topology_snapshot_from_json(
        topology_snapshot_to_json(snapshot), error);
    INFO(error);
    REQUIRE(decoded == snapshot);
    REQUIRE_FALSE(decoded->spaces.front()
                      .tabs.front()
                      .name_user_set);

    auto legacy_json = topology_snapshot_to_json(snapshot);
    legacy_json["spaces"][0]["tabs"][0].erase(
        "name_user_set");
    const auto legacy_decoded
        = topology_snapshot_from_json(legacy_json, error);
    INFO(error);
    REQUIRE(legacy_decoded);
    REQUIRE(legacy_decoded->spaces.front()
                .tabs.front()
                .name_user_set);

    TopologyCommand command{
        .client_id = "client-a",
        .command_id = "command-1",
        .expected_revision = 7,
        .kind = TopologyCommandKind::SplitPane,
        .space_id = "space-1",
        .tab_id = "tab-1",
        .pane_id = "pane-1",
        .name = "Local shell",
        .direction = TopologySplitDirection::Horizontal,
        .pane_domain = TopologyPaneDomain::ClientLocal,
        .client_host_kind = "platform_default",
    };
    const auto decoded_command = topology_command_from_json(
        topology_command_to_json(command), error);
    INFO(error);
    REQUIRE(decoded_command == command);

    TopologyCommand reorder{
        .client_id = "client-a",
        .command_id = "command-2",
        .expected_revision = 8,
        .kind = TopologyCommandKind::SwapPane,
        .space_id = "space-1",
        .tab_id = "tab-1",
        .pane_id = "pane-1",
        .target_pane_id = "pane-2",
        .move_delta = -1,
    };
    const auto decoded_reorder = topology_command_from_json(
        topology_command_to_json(reorder), error);
    INFO(error);
    REQUIRE(decoded_reorder == reorder);
}

TEST_CASE("topology protocol rejects dangling split children",
    "[server][protocol][topology]")
{
    TopologySnapshot snapshot{
        .revision = 1,
        .session_id = "default",
        .spaces = {
            {
                .space_id = "space-1",
                .name = "Broken",
                .tabs = {
                    {
                        .tab_id = "tab-1",
                        .name = "Tab",
                        .root_node_id = "split-1",
                        .nodes = {
                            {
                                .node_id = "split-1",
                                .is_leaf = false,
                                .first_node_id = "missing",
                                .second_node_id = "also-missing",
                            },
                        },
                        .panes = {
                            {
                                .pane_id = "pane-1",
                                .domain = TopologyPaneDomain::ClientLocal,
                                .client_host_kind = "nvim",
                            },
                        },
                    },
                },
            },
        },
    };
    std::string error;
    REQUIRE_FALSE(topology_snapshot_from_json(
        topology_snapshot_to_json(snapshot), error));
}

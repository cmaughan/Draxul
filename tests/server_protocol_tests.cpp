#include <catch2/catch_test_macros.hpp>

#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>
#include <draxul/topology_protocol.h>

#include <nlohmann/json.hpp>

using namespace draxul;

TEST_CASE("Session stream protocol round-trips open and framed updates",
    "[server][protocol][session-stream]")
{
    SessionPollRequest poll{
        .request_serial = 9,
        .server_epoch = "stream-epoch",
        .topology_after_revision = 4,
        .agent_after_revision = 5,
        .terminals = {
            {
                .subscription_id = 3,
                .terminal_id = "terminal-a",
                .visibility_generation = 2,
                .visible = true,
                .cursor = SessionTerminalCursor{
                    .generation = 7,
                    .after_sequence = 11,
                },
            },
        },
    };
    const SessionStreamOpenRequest open{
        .server_epoch = "stream-epoch",
        .session_id = "default",
        .poll = poll,
    };
    std::string error;
    const auto decoded_open = session_stream_open_request_from_json(
        session_stream_open_request_to_json(open), error);
    REQUIRE(decoded_open);
    CHECK(*decoded_open == open);

    const SessionStreamClientFrame update{
        .kind = SessionStreamClientFrameKind::Update,
        .update = SessionStreamUpdate{ .poll = poll },
    };
    const auto decoded_update = session_stream_client_frame_from_json(
        session_stream_client_frame_to_json(update), error);
    REQUIRE(decoded_update);
    CHECK(*decoded_update == update);

    SessionPollResponse events{
        .request_serial = poll.request_serial,
        .server_epoch = poll.server_epoch,
    };
    const SessionStreamServerFrame frame{
        .kind = SessionStreamServerFrameKind::Events,
        .frame_serial = 17,
        .server_epoch = "stream-epoch",
        .events = events,
    };
    const auto decoded_frame = session_stream_server_frame_from_json(
        session_stream_server_frame_to_json(frame), error);
    REQUIRE(decoded_frame);
    CHECK(*decoded_frame == frame);
}

TEST_CASE("Session stream protocol rejects unsafe negotiated limits",
    "[server][protocol][session-stream][bounds]")
{
    nlohmann::json response{
        { "server_epoch", "stream-epoch" },
        { "endpoint", "endpoint" },
        { "ticket", "ticket" },
        { "heartbeat_interval_ms",
            kSessionStreamDefaultHeartbeatIntervalMs },
        { "max_frame_bytes", kSessionStreamMaxFrameBytes + 1 },
        { "max_queue_bytes", kSessionStreamDefaultQueueBytes },
    };
    std::string error;
    CHECK_FALSE(session_stream_open_response_from_json(response, error));
    response["max_frame_bytes"] = kSessionStreamMaxFrameBytes;
    response["max_queue_bytes"] = kSessionStreamMaxQueueBytes + 1;
    CHECK_FALSE(session_stream_open_response_from_json(response, error));
    response["max_queue_bytes"] = kSessionStreamMinQueueBytes - 1;
    CHECK_FALSE(session_stream_open_response_from_json(response, error));
    response["max_queue_bytes"] = kSessionStreamDefaultQueueBytes;
    response["heartbeat_interval_ms"]
        = kSessionStreamMinHeartbeatIntervalMs - 1;
    CHECK_FALSE(session_stream_open_response_from_json(response, error));
}

TEST_CASE("Session poll protocol preserves subscription and visibility identity",
    "[server][protocol][session-poll]")
{
    SessionPollRequest request{
        .request_serial = 7,
        .server_epoch = "epoch-a",
        .topology_after_revision = 4,
        .agent_after_revision = 5,
        .terminals = {
            {
                .subscription_id = 11,
                .terminal_id = "terminal-a",
                .visibility_generation = 3,
                .visible = true,
                .cursor = std::nullopt,
            },
            {
                .subscription_id = 12,
                .terminal_id = "terminal-a",
                .visibility_generation = 8,
                .visible = false,
                .cursor = SessionTerminalCursor{
                    .generation = 2,
                    .after_sequence = 19,
                },
            },
        },
    };
    const auto encoded = session_poll_request_to_json(request);
    REQUIRE(encoded["terminals"][0]["cursor"].is_null());
    std::string error;
    const auto decoded = session_poll_request_from_json(encoded, error);
    INFO(error);
    REQUIRE(decoded);
    CHECK(*decoded == request);

    SessionPollResponse response{
        .request_serial = request.request_serial,
        .server_epoch = request.server_epoch,
        .topology = { .revision = 4 },
        .agents = { .revision = 5 },
        .terminals = {
            {
                .subscription_id = 11,
                .terminal_id = "terminal-a",
                .visibility_generation = 3,
                .suspended = false,
                .resync = true,
            },
            {
                .subscription_id = 12,
                .terminal_id = "terminal-a",
                .visibility_generation = 8,
                .suspended = true,
            },
        },
    };
    const auto decoded_response = session_poll_response_from_json(
        session_poll_response_to_json(response), error);
    INFO(error);
    REQUIRE(decoded_response);
    CHECK(*decoded_response == response);
}

TEST_CASE("Session poll protocol rejects hostile subscription envelopes",
    "[server][protocol][session-poll]")
{
    SessionPollRequest request{
        .request_serial = 1,
        .server_epoch = "epoch-a",
        .terminals = {
            {
                .subscription_id = 1,
                .terminal_id = "terminal-a",
                .visibility_generation = 1,
            },
            {
                .subscription_id = 1,
                .terminal_id = "terminal-b",
                .visibility_generation = 1,
            },
        },
    };
    std::string error;
    CHECK_FALSE(session_poll_request_from_json(
        session_poll_request_to_json(request), error));

    auto missing_epoch = session_poll_request_to_json(request);
    missing_epoch["server_epoch"] = "";
    CHECK_FALSE(session_poll_request_from_json(
        missing_epoch, error));

    request.terminals.resize(kSessionPollMaxSubscriptions + 1,
        request.terminals.front());
    for (size_t index = 0; index < request.terminals.size(); ++index)
        request.terminals[index].subscription_id = index + 1;
    CHECK_FALSE(session_poll_request_from_json(
        session_poll_request_to_json(request), error));
}

TEST_CASE("server protocol round-trips hello welcome and status", "[server][protocol]")
{
    const ServerHello hello{
        .client_id = "ui-test",
        .connection_token = "hello-token",
        .registration_nonce = "hello-nonce",
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
        .connection_token = "welcome-token",
        .capabilities = { "status" },
    };
    const auto decoded_welcome = server_welcome_from_json(
        server_welcome_to_json(welcome), error);
    REQUIRE(decoded_welcome == welcome);

    auto legacy_hello = server_hello_to_json(hello);
    legacy_hello.erase("connection_token");
    legacy_hello.erase("registration_nonce");
    auto expected_legacy_hello = hello;
    expected_legacy_hello.connection_token.clear();
    expected_legacy_hello.registration_nonce.clear();
    REQUIRE(server_hello_from_json(legacy_hello, error)
        == expected_legacy_hello);

    auto legacy_welcome = server_welcome_to_json(welcome);
    legacy_welcome.erase("connection_token");
    auto expected_legacy_welcome = welcome;
    expected_legacy_welcome.connection_token.clear();
    REQUIRE(server_welcome_from_json(legacy_welcome, error)
        == expected_legacy_welcome);

    const ServerStatusSnapshot status{
        .state = "ready",
        .protocol_major = 1,
        .protocol_minor = 0,
        .server_pid = 42,
        .server_epoch = "epoch",
        .build_version = "test",
        .uptime_ms = 123,
        .connected_clients = 2,
        .scrollback_cells_reserved = 800'000,
        .scrollback_cells_limit = 24'000'000,
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
        .control_transport = {
            .listener_capacity = 4,
            .accepted_connections = 8,
            .active_connections = 1,
            .peak_connections = 3,
            .requests = 7,
            .failed_requests = 1,
            .invalid_frames = 2,
            .methods = {
                {
                    .method = "session.poll",
                    .requests = 7,
                    .failures = 1,
                    .queue_time = { 7, 70, 20 },
                    .dispatch_time = { 7, 140, 40 },
                    .response_time = { 7, 210, 60 },
                },
            },
            .transport_failures = {
                { "read", "read_prefix", "win32", "io_error", 109, 2 },
            },
        },
    };
    const auto decoded_status = server_status_from_json(
        server_status_to_json(status), error);
    REQUIRE(decoded_status == status);

    auto legacy_status = server_status_to_json(status);
    legacy_status.erase("control_transport");
    auto expected_legacy_status = status;
    expected_legacy_status.control_transport = {};
    REQUIRE(server_status_from_json(legacy_status, error)
        == expected_legacy_status);
}

TEST_CASE("server protocol rejects malformed identity and capabilities", "[server][protocol]")
{
    std::string error;
    auto hello_json = server_hello_to_json({
        .client_id = "",
    });
    REQUIRE_FALSE(server_hello_from_json(hello_json, error));

    hello_json = server_hello_to_json({
        .client_id = "client",
        .registration_nonce = "nonce\nforged",
    });
    REQUIRE_FALSE(server_hello_from_json(hello_json, error));

    auto welcome_json = server_welcome_to_json({
        .protocol_major = 1,
        .server_pid = 1,
        .server_epoch = "epoch",
        .capabilities = { "status", "status" },
    });
    REQUIRE_FALSE(server_welcome_from_json(welcome_json, error));

    hello_json = server_hello_to_json({
        .client_id = "client",
        .connection_token
            = std::string(kServerMaxConnectionTokenBytes + 1, 'x'),
    });
    REQUIRE_FALSE(server_hello_from_json(hello_json, error));

    welcome_json = server_welcome_to_json({
        .protocol_major = kServerProtocolMajor,
        .server_pid = 1,
        .server_epoch = "epoch",
        .connection_token = "token\nforged",
    });
    REQUIRE_FALSE(server_welcome_from_json(welcome_json, error));
}

TEST_CASE("server protocol rejects narrowing overflow and hostile status values",
    "[server][protocol][validation]")
{
    std::string error;
    auto hello = server_hello_to_json({
        .client_id = "client",
    });
    hello["protocol_major"]
        = uint64_t{ 4'294'967'297 };
    CHECK_FALSE(server_hello_from_json(hello, error));

    hello = server_hello_to_json({
        .client_id = "client\nforged-log-line",
    });
    CHECK_FALSE(server_hello_from_json(hello, error));
    CHECK_FALSE(valid_server_client_id(
        "client\x1b[31m"));

    ServerStatusSnapshot status{
        .state = "ready",
        .protocol_major = kServerProtocolMajor,
        .protocol_minor = kServerProtocolMinor,
        .server_pid = 42,
        .server_epoch = "epoch",
        .build_version = "test",
    };
    auto encoded = server_status_to_json(status);
    encoded["connected_clients"] = -1;
    CHECK_FALSE(server_status_from_json(encoded, error));

    encoded = server_status_to_json(status);
    encoded["state"] = std::string(
        kServerMaxStatusStateBytes + 1, 'x');
    CHECK_FALSE(server_status_from_json(encoded, error));

    encoded = server_status_to_json(status);
    encoded["server_epoch"] = std::string(
        kServerMaxHandshakeTextBytes + 1, 'x');
    CHECK_FALSE(server_status_from_json(encoded, error));

    encoded = server_status_to_json(status);
    encoded["session_statuses"]
        = nlohmann::json::array();
    for (size_t index = 0;
         index <= kServerMaxSessions; ++index)
    {
        encoded["session_statuses"].push_back({
            { "session_id", "session-" + std::to_string(index) },
            { "spaces", 0 },
            { "terminals", 0 },
            { "live_terminals", 0 },
        });
    }
    CHECK_FALSE(server_status_from_json(encoded, error));

    encoded = server_status_to_json(status);
    encoded["control_transport"]["active_connections"] = 2;
    encoded["control_transport"]["peak_connections"] = 1;
    CHECK_FALSE(server_status_from_json(encoded, error));

    encoded = server_status_to_json(status);
    encoded["control_transport"]["methods"]
        = nlohmann::json::array();
    for (size_t index = 0; index < 65; ++index)
    {
        encoded["control_transport"]["methods"].push_back({
            { "method", "method-" + std::to_string(index) },
            { "requests", 1 },
            { "failures", 0 },
            { "queue_time", {
                  { "samples", 1 }, { "total_us", 1 }, { "max_us", 1 } } },
            { "dispatch_time", {
                  { "samples", 1 }, { "total_us", 1 }, { "max_us", 1 } } },
            { "response_time", {
                  { "samples", 1 }, { "total_us", 1 }, { "max_us", 1 } } },
        });
    }
    CHECK_FALSE(server_status_from_json(encoded, error));
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
            .process_running = false,
            .exit_code = 0,
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
            .process_running = false,
            .exit_code = 0,
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
                                .client_working_directory = "D:/dev/Draxul",
                                .client_source_path = "notes/readme.md",
                                .client_plugin_id
                                    = "dev.draxul.spinning-triangle",
                                .client_plugin_config_json
                                    = R"({"paused":true})",
                                .companion_owner_pane_id = "pane-1",
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
        .client_working_directory = "D:/dev/Draxul",
        .client_source_path = "notes/readme.md",
        .client_plugin_id
            = "dev.draxul.spinning-triangle",
        .client_plugin_config_json
            = R"({"paused":true})",
        .companion_owner_pane_id = "pane-1",
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

    const nlohmann::json minimal_command{
        { "client_id", "client-a" },
        { "command_id", "command-minimal" },
        { "expected_revision", 9 },
        { "kind", "create_space" },
    };
    const TopologyCommand expected_minimal{
        .client_id = "client-a",
        .command_id = "command-minimal",
        .expected_revision = 9,
    };
    const auto decoded_minimal
        = topology_command_from_json(minimal_command, error);
    INFO(error);
    REQUIRE(decoded_minimal == expected_minimal);
    REQUIRE(topology_command_from_json(
                topology_command_to_json(*decoded_minimal), error)
        == decoded_minimal);

    for (std::string_view required :
        { "client_id", "command_id", "expected_revision", "kind" })
    {
        auto missing_required = minimal_command;
        missing_required.erase(std::string(required));
        CHECK_FALSE(topology_command_from_json(
            missing_required, error));
    }

    auto malformed_optional = minimal_command;
    malformed_optional["space_id"] = 42;
    CHECK_FALSE(topology_command_from_json(
        malformed_optional, error));
    malformed_optional = minimal_command;
    malformed_optional["direction"] = "diagonal";
    CHECK_FALSE(topology_command_from_json(
        malformed_optional, error));
    malformed_optional = minimal_command;
    malformed_optional["ratio"] = "half";
    CHECK_FALSE(topology_command_from_json(
        malformed_optional, error));
    malformed_optional = minimal_command;
    malformed_optional["pane_domain"] = "somewhere";
    CHECK_FALSE(topology_command_from_json(
        malformed_optional, error));

    auto oversized_move = topology_command_to_json(reorder);
    oversized_move["move_delta"]
        = uint64_t{ 4'294'967'297 };
    CHECK_FALSE(topology_command_from_json(
        oversized_move, error));

    auto hostile_client = topology_command_to_json(reorder);
    hostile_client["client_id"] = "client\rforged";
    CHECK_FALSE(topology_command_from_json(
        hostile_client, error));

    TopologyCommandResult command_result{
        .applied = true,
        .created_id = "pane-created",
        .snapshot = snapshot,
    };
    const auto decoded_result
        = topology_command_result_from_json(
            topology_command_result_to_json(command_result),
            error);
    INFO(error);
    REQUIRE(decoded_result == command_result);

    auto legacy_result
        = topology_command_result_to_json(command_result);
    legacy_result.erase("created_id");
    const auto decoded_legacy_result
        = topology_command_result_from_json(
            legacy_result, error);
    INFO(error);
    REQUIRE(decoded_legacy_result);
    CHECK(decoded_legacy_result->created_id.empty());

    auto oversized_result
        = topology_command_result_to_json(command_result);
    oversized_result["created_id"]
        = std::string(kTopologyMaxTextBytes + 1, 'x');
    CHECK_FALSE(topology_command_result_from_json(
        oversized_result, error));
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

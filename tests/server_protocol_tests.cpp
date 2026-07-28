#include <catch2/catch_test_macros.hpp>

#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>

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
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 2,
                .sequence = 7,
            },
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

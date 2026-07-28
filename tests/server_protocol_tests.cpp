#include <catch2/catch_test_macros.hpp>

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

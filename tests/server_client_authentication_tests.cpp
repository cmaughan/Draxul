#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

TEST_CASE("server connection tokens bind active client identities",
    "[server][kernel][clients][security]")
{
    TempDir temp("draxul-server-client-token");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "token-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto options = probe_options(temp.path);
    options.client_id = "bound-client";
    options.registration_nonce = "bound-registration-nonce";
    const auto probe = ServerClient::probe(options);
    REQUIRE(probe.ready());
    const std::string token
        = probe.welcome->connection_token;
    REQUIRE_FALSE(token.empty());

    const auto retried_hello = ControlClient::request(
        namespaced_control_id(
            kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "bound-client",
            .registration_nonce
            = options.registration_nonce,
            .capabilities = {
                std::string(kServerClientTokenCapability),
            },
        }));
    REQUIRE(retried_hello.ok);
    std::string welcome_error;
    const auto retried_welcome
        = server_welcome_from_json(
            retried_hello.result, welcome_error);
    INFO(welcome_error);
    REQUIRE(retried_welcome);
    CHECK(retried_welcome->connection_token == token);

    const auto request = [&](std::string_view method,
                             nlohmann::json params) {
        return ControlClient::request(
            namespaced_control_id(
                kServerControlId, temp.path),
            temp.path, method, std::move(params));
    };
    const auto missing = request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
        });
    REQUIRE_FALSE(missing.ok);
    CHECK(missing.error_code
        == "invalid_connection_token");
    const auto wrong = request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
            { "connection_token", "wrong-token" },
        });
    REQUIRE_FALSE(wrong.ok);
    CHECK(wrong.error_code
        == "invalid_connection_token");
    REQUIRE(request(
        "fake.attach",
        {
            { "client_id", "bound-client" },
            { "connection_token", token },
        })
            .ok);

    const auto spoofed_input = request(
        "fake.input",
        {
            { "client_id", "bound-client" },
            { "request_id", uint64_t{ 1 } },
            { "text", "spoofed" },
        });
    REQUIRE_FALSE(spoofed_input.ok);
    CHECK(spoofed_input.error_code
        == "invalid_connection_token");
    REQUIRE(request(
        "fake.input",
        {
            { "client_id", "bound-client" },
            { "connection_token", token },
            { "request_id", uint64_t{ 2 } },
            { "text", "accepted" },
        })
            .ok);

    const auto wrong_hello = request(
        "server.hello",
        server_hello_to_json({
            .client_id = "bound-client",
            .connection_token = "wrong-token",
            .registration_nonce = "wrong-nonce",
            .capabilities = {
                std::string(kServerClientTokenCapability),
            },
        }));
    REQUIRE_FALSE(wrong_hello.ok);
    CHECK(wrong_hello.error_code
        == "invalid_connection_token");

    std::string error;
    REQUIRE_FALSE(ServerClient::disconnect(
        temp.path, "bound-client", error, "wrong-token"));
    const auto connected_status
        = ServerClient::status(temp.path);
    REQUIRE(connected_status.ok);
    REQUIRE(connected_status.status->connected_clients == 1);
    REQUIRE(ServerClient::disconnect(
        temp.path, "bound-client", error, token));
    const auto disconnected_status
        = ServerClient::status(temp.path);
    REQUIRE(disconnected_status.ok);
    REQUIRE(disconnected_status.status->connected_clients == 0);

    run_guard.join();

    ServerKernel replacement({
        .runtime_directory = temp.path,
        .epoch_override = "replacement-token-epoch",
    });
    REQUIRE(replacement.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard replacement_guard(replacement);
    options.connection_token = token;
    const auto replaced = ServerClient::probe(options);
    REQUIRE(replaced.ready());
    CHECK(replaced.welcome->server_epoch
        == "replacement-token-epoch");
    CHECK(replaced.welcome->connection_token != token);
    REQUIRE(ServerClient::disconnect(
        temp.path, "bound-client", error,
        replaced.welcome->connection_token));
    replacement_guard.join();
}

TEST_CASE("server releases expired terminal leases",
    "[server][kernel][clients]")
{
    TempDir temp("draxul-server-client-leases");
    ServerKernel server({
        .runtime_directory = temp.path,
        // Keep setup RPCs comfortably inside the lease on Windows debug
        // builds; the one-second sleep below still proves expiry.
        .client_activity_timeout = std::chrono::milliseconds(500),
        .build_version = "unit-test",
        .epoch_override = "lease-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto terminal = remote_client(
        temp.path, "expiring-client", "lease-epoch");
    std::string error;
    REQUIRE(terminal.attach(error));
    CHECK_FALSE(terminal.resize(
        kRemoteTerminalMaxColumns + 1, 10, error));
    CHECK(terminal.last_error_code() == "invalid_resize");
    REQUIRE(terminal.suspend(error, 1));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool changed = false;
    CHECK_FALSE(terminal.poll(changed, error));
    CHECK(terminal.last_error_code() == "not_attached");
    REQUIRE(terminal.attach(error));
    REQUIRE(terminal.send_input("reattached", error));

    REQUIRE(ServerClient::disconnect(
        temp.path, "expiring-client", error));
}

TEST_CASE("server bounds the connected client registry",
    "[server][kernel][clients]")
{
    TempDir temp("draxul-server-client-limit");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "client-limit-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    for (size_t index = 0;
        index < kServerMaxConnectedClients;
        ++index)
    {
        const auto hello = ControlClient::request(
            namespaced_control_id(
                kServerControlId, temp.path),
            temp.path, "server.hello",
            server_hello_to_json({
                .client_id = "bounded-client-"
                    + std::to_string(index),
            }));
        INFO(index);
        REQUIRE(hello.ok);
    }
    const auto rejected = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "one-client-too-many",
        }));
    CHECK_FALSE(rejected.ok);
    CHECK(rejected.error_code == "client_limit_reached");

    std::string error;
    REQUIRE(ServerClient::disconnect(
        temp.path, "bounded-client-0", error));
    const auto admitted = ControlClient::request(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, "server.hello",
        server_hello_to_json({
            .client_id = "replacement-client",
        }));
    REQUIRE(admitted.ok);
}

TEST_CASE("two remote terminal clients converge through control takeover and reconnect",
    "[server][remote-terminal]")
{
    TempDir temp("draxul-fake-remote-terminal");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client_a = remote_client(temp.path, "client-a");
    auto client_b = remote_client(temp.path, "client-b");
    std::string error;
    REQUIRE(client_a.attach(error));
    INFO(error);
    REQUIRE(client_b.attach(error));
    INFO(error);
    REQUIRE(client_a.projection().is_controller("client-a"));
    REQUIRE_FALSE(client_b.projection().is_controller("client-b"));
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE(client_a.send_input("shared", error));
    bool changed = false;
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE_FALSE(client_b.send_input("denied", error));
    REQUIRE(client_b.last_error_code() == "not_controller");
    REQUIRE(client_b.take_control(error));
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(changed);
    REQUIRE_FALSE(client_a.projection().is_controller("client-a"));
    REQUIRE(client_b.projection().is_controller("client-b"));

    REQUIRE(client_b.resize(48, 14, error));
    REQUIRE(client_a.poll(changed, error));
    REQUIRE(client_b.poll(changed, error));
    REQUIRE(client_a.projection().snapshot().cols == 48);
    REQUIRE(client_a.projection().snapshot().rows == 14);
    REQUIRE(terminal_semantic_digest(client_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));

    REQUIRE(client_a.disconnect(error));
    REQUIRE(client_b.send_input("\rreconnected", error));
    REQUIRE(client_b.poll(changed, error));

    auto reconnected_a = remote_client(temp.path, "client-a");
    REQUIRE(reconnected_a.attach(error));
    INFO(error);
    REQUIRE(terminal_semantic_digest(reconnected_a.projection().snapshot())
        == terminal_semantic_digest(client_b.projection().snapshot()));
    REQUIRE(reconnected_a.projection().version()
        == client_b.projection().version());

    run_guard.join();
}

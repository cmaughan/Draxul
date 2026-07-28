#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/control_plane.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>

#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace draxul;
using draxul::tests::TempDir;

namespace
{

uint64_t test_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

ServerEnsureOptions probe_options(const std::filesystem::path& runtime)
{
    return {
        .runtime_directory = runtime,
        .client_id = "unit-client",
        .timeout = std::chrono::seconds(2),
        .launch_if_missing = false,
    };
}

}

TEST_CASE("server kernel publishes one identity and stops gracefully", "[server][kernel]")
{
    TempDir temp("draxul-server-kernel");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);

    std::jthread run_thread([&server] { server.run_until_stopped(); });
    const auto probe = ServerClient::probe(probe_options(temp.path));
    REQUIRE(probe.ready());
    REQUIRE(probe.welcome->server_pid == server.process_id());
    REQUIRE(probe.welcome->server_epoch == "fixed-epoch");

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->connected_clients == 1);
    REQUIRE(status.status->terminals == 0);

    ServerKernel duplicate({
        .runtime_directory = temp.path,
    });
    REQUIRE(duplicate.start().disposition
        == ServerStartDisposition::AlreadyRunning);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path, shutdown_error));
    run_thread.join();
    REQUIRE_FALSE(server.running());
    REQUIRE_FALSE(std::filesystem::exists(server_metadata_path(temp.path)));
}

TEST_CASE("server client classifies absent starting stale and crashed runtimes", "[server][discovery]")
{
    TempDir temp("draxul-server-discovery");
    auto options = probe_options(temp.path);
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Absent);

    const auto marker = temp.path
        / ("server-starting-" + std::to_string(test_process_id()) + ".json");
    {
        std::ofstream output(marker);
        output << nlohmann::json{ { "pid", test_process_id() } }.dump();
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Starting);
    std::filesystem::remove(marker);

    {
        std::ofstream output(server_metadata_path(temp.path));
        output << "{broken";
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Stale);

    {
        std::ofstream output(server_metadata_path(temp.path));
        output << nlohmann::json{
            { "version", 1 },
            { "endpoint", R"(\\.\pipe\draxul-definitely-absent)" },
            { "token", std::string(64, 'a') },
            { "server_pid", uint64_t{ 999999999 } },
        }
                      .dump();
    }
    REQUIRE(ServerClient::probe(options).state == ServerProbeState::Crashed);
}

TEST_CASE("server endpoint namespaces follow the runtime directory", "[server][discovery]")
{
    TempDir first("draxul-server-namespace-a");
    TempDir second("draxul-server-namespace-b");

    REQUIRE(namespaced_control_id(kServerControlId, first.path)
        == namespaced_control_id(kServerControlId,
            first.path / "." / ".." / first.path.filename()));
    REQUIRE(namespaced_control_id(kServerControlId, first.path)
        != namespaced_control_id(kServerControlId, second.path));
}

TEST_CASE("server rejects an incompatible protocol major", "[server][protocol]")
{
    TempDir temp("draxul-server-incompatible");
    ServerKernel server({
        .runtime_directory = temp.path,
        .protocol_major = 7,
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    std::jthread run_thread([&server] { server.run_until_stopped(); });

    REQUIRE(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Incompatible);
    server.request_stop();
    run_thread.join();
}

TEST_CASE("server client distinguishes a live but unresponsive listener", "[server][discovery]")
{
    TempDir temp("draxul-server-busy");
    ControlServer unresponsive;
    std::string start_error;
    REQUIRE(unresponsive.start(
        namespaced_control_id(kServerControlId, temp.path), temp.path,
        [] {}, &start_error,
        { { "server_pid", test_process_id() } }));

    REQUIRE(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Busy);
    unresponsive.stop();
}

#ifdef DRAXUL_EXECUTABLE_PATH
TEST_CASE("ten concurrent clients converge on one detached server epoch", "[server][process]")
{
    TempDir temp("draxul-server-process");
    const std::filesystem::path executable = DRAXUL_EXECUTABLE_PATH;

    std::vector<std::future<ServerProbeResult>> clients;
    for (int index = 0; index < 10; ++index)
    {
        clients.push_back(std::async(std::launch::async,
            [runtime = temp.path, executable, index] {
                return ServerClient::ensure({
                    .runtime_directory = runtime,
                    .executable_path = executable,
                    .client_id = "process-client-" + std::to_string(index),
                    .timeout = std::chrono::seconds(15),
                });
            }));
    }

    std::optional<ServerWelcome> identity;
    for (auto& client : clients)
    {
        const auto result = client.get();
        INFO(result.error_message);
        REQUIRE(result.ready());
        if (!identity)
            identity = result.welcome;
        REQUIRE(result.welcome->server_pid == identity->server_pid);
        REQUIRE(result.welcome->server_epoch == identity->server_epoch);
    }

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->connected_clients == 10);
    REQUIRE(status.status->terminals == 0);
    REQUIRE(status.status->spaces == 0);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path, shutdown_error));
    for (int attempt = 0;
         attempt < 200
         && std::filesystem::exists(server_metadata_path(temp.path));
         ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE_FALSE(std::filesystem::exists(server_metadata_path(temp.path)));
}
#endif

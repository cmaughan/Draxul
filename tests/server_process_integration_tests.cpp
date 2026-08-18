#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

#ifdef DRAXUL_EXECUTABLE_PATH
#ifdef _WIN32
bool set_process_suspended(
    uint64_t process_id, bool suspend)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool changed = false;
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID
                != static_cast<DWORD>(process_id))
            {
                continue;
            }
            HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME, FALSE,
                entry.th32ThreadID);
            if (!thread)
                continue;
            const DWORD result = suspend
                ? SuspendThread(thread)
                : ResumeThread(thread);
            changed = changed || result != static_cast<DWORD>(-1);
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return changed;
}
#endif

TEST_CASE("ensure relaunches past a recycled PID and an expired startup marker",
    "[server][process][discovery][recovery]")
{
    const std::filesystem::path executable
        = DRAXUL_EXECUTABLE_PATH;

    auto ensure_and_stop
        = [&executable](const std::filesystem::path& runtime) {
              ServerEnsureOptions options{
                  .runtime_directory = runtime,
                  .executable_path = executable,
                  .client_id = "recovery-client",
                  .timeout = std::chrono::seconds(15),
              };
              const auto recovered
                  = ServerClient::ensure(options);
              INFO(recovered.error_message);
              REQUIRE(recovered.ready());
              std::string shutdown_error;
              REQUIRE(ServerClient::shutdown(runtime,
                  { .confirm_live_terminals = true },
                  shutdown_error));
              for (int attempt = 0;
                  attempt < 200
                  && std::filesystem::exists(
                      server_metadata_path(runtime));
                  ++attempt)
              {
                  std::this_thread::sleep_for(
                      std::chrono::milliseconds(10));
              }
              REQUIRE_FALSE(std::filesystem::exists(
                  server_metadata_path(runtime)));
          };

    TempDir recycled("draxul-server-recycled-pid");
    {
        std::ofstream output(
            server_metadata_path(recycled.path));
        output << nlohmann::json{
            { "version", kControlProtocolVersion },
            { "endpoint",
                R"(\\.\pipe\draxul-recycled-pid)" },
            { "token", std::string(64, 'a') },
            { "server_pid", test_process_id() },
            { "server_process_start_token",
                "a-different-process-incarnation" },
            { "published_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    ensure_and_stop(recycled.path);

    TempDir expired("draxul-server-expired-start");
    const auto marker = expired.path
        / ("server-starting-"
            + std::to_string(test_process_id())
            + ".json");
    {
        std::ofstream output(marker);
        output << nlohmann::json{
            { "pid", test_process_id() },
            { "process_start_token",
                test_process_start_token() },
            { "created_unix_ms", uint64_t{ 1 } },
        }
                      .dump();
    }
    ensure_and_stop(expired.path);
    CHECK_FALSE(std::filesystem::exists(marker));
}

TEST_CASE("force stop uses published process identity when the server is unresponsive",
    "[server][process][discovery][recovery]")
{
    TempDir temp("draxul-server-force-stop-wedged");
    auto options = probe_options(temp.path);
    options.executable_path = DRAXUL_EXECUTABLE_PATH;
    options.launch_if_missing = true;
    options.timeout = std::chrono::seconds(15);
    const auto running = ServerClient::ensure(options);
    INFO(running.error_message);
    REQUIRE(running.ready());
    const uint64_t server_pid = running.welcome->server_pid;

#ifdef _WIN32
    REQUIRE(set_process_suspended(server_pid, true));
#else
    REQUIRE(::kill(static_cast<pid_t>(server_pid), SIGSTOP) == 0);
#endif
    std::string stop_error;
    const bool stopped = ServerClient::force_stop(
        temp.path, true, stop_error);
    if (!stopped)
    {
#ifdef _WIN32
        set_process_suspended(server_pid, false);
#else
        ::kill(static_cast<pid_t>(server_pid), SIGCONT);
#endif
    }
    INFO(stop_error);
    REQUIRE(stopped);

    bool alive = true;
    for (int attempt = 0; attempt < 200 && alive;
        ++attempt)
    {
#ifdef _WIN32
        HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, static_cast<DWORD>(server_pid));
        if (!process)
            alive = false;
        else
        {
            DWORD exit_code = STILL_ACTIVE;
            alive = GetExitCodeProcess(process, &exit_code)
                && exit_code == STILL_ACTIVE;
            CloseHandle(process);
        }
#else
        alive = ::kill(static_cast<pid_t>(server_pid), 0) == 0;
#endif
        if (alive)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }
    CHECK_FALSE(alive);
}

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
    REQUIRE(status.status->terminals == 1);
    REQUIRE(status.status->sessions == 1);
    REQUIRE(status.status->spaces == 1);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
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

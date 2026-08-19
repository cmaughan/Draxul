#pragma once

#include "../../libs/draxul-server/src/fake_terminal_runtime.h"
#include "../../libs/draxul-server/src/remote_terminal_service.h"
#include "server_agent_service.h"
#include "../../libs/draxul-server/src/server_terminal_runtime.h"
#include "../../libs/draxul-server/src/session_poll_service.h"
#include "../../libs/draxul-server/src/session_topology_bridge.h"
#include "../../libs/draxul-server/src/topology_service.h"
#include "temp_dir.h"

#include <draxul/agent_client.h>
#include <draxul/agent_protocol.h>
#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>
#include <draxul/session_state.h>
#include <draxul/topology_client.h>

#include <cstdlib>
#include <fstream>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <tlhelp32.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace draxul::tests::server_kernel
{

inline uint64_t test_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

inline std::string test_process_start_token()
{
    static const std::string token = [] {
        TempDir identity_runtime(
            "draxul-server-process-identity");
        ServerKernel identity_server({
            .runtime_directory = identity_runtime.path,
            .build_version = "identity-test",
        });
        if (identity_server.start().disposition
            != ServerStartDisposition::Started)
        {
            return std::string{};
        }
        std::ifstream input(
            server_metadata_path(identity_runtime.path));
        const auto metadata
            = nlohmann::json::parse(input, nullptr, false);
        identity_server.stop();
        return metadata.is_object()
                && metadata.contains(
                    "server_process_start_token")
                && metadata["server_process_start_token"]
                       .is_string()
            ? metadata["server_process_start_token"]
                  .get<std::string>()
            : std::string{};
    }();
    return token;
}

inline ServerEnsureOptions probe_options(const std::filesystem::path& runtime)
{
    return {
        .runtime_directory = runtime,
        .client_id = "unit-client",
        .timeout = std::chrono::seconds(2),
        .launch_if_missing = false,
    };
}

inline RemoteTerminalClient remote_client(
    const std::filesystem::path& runtime,
    std::string client_id,
    std::string epoch = "fixed-epoch",
    std::string method_prefix = "fake",
    std::shared_ptr<ClientRecoveryState> recovery = {})
{
    return RemoteTerminalClient({
        .runtime_directory = runtime,
        .client_id = std::move(client_id),
        .expected_server_epoch = std::move(epoch),
        .method_prefix = std::move(method_prefix),
        .recovery = std::move(recovery),
    });
}

inline std::string snapshot_text(const TerminalSemanticSnapshot& snapshot)
{
    std::string text;
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
        {
            text += snapshot.cells[static_cast<size_t>(row) * snapshot.cols + col]
                        .text;
        }
        text.push_back('\n');
    }
    return text;
}

inline bool wait_for_text(
    RemoteTerminalClient& client, std::string_view expected,
    std::string& error)
{
    for (int attempt = 0; attempt < 600; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (snapshot_text(client.projection().snapshot()).find(expected)
            != std::string::npos)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    error = "Timed out waiting for terminal text.";
    return false;
}

inline bool wait_for_alternate_screen(
    RemoteTerminalClient& client, bool expected, std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (client.projection().snapshot().metadata.modes.alternate_screen
            == expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    error = expected
        ? "Timed out waiting to enter the alternate screen."
        : "Timed out waiting to leave the alternate screen.";
    return false;
}

inline bool wait_for_agent(AgentClient& client,
    std::string_view kind, std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        if (std::ranges::any_of(
                client.snapshot().agents,
                [kind](const ServerAgentProjection& agent) {
                    return agent.identity.kind == kind;
                }))
        {
            return true;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    }
    error = "Timed out waiting for the shared agent projection.";
    return false;
}

#ifdef _WIN32
inline uint64_t parent_process_id(uint64_t process_id)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uint64_t result = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == process_id)
            {
                result = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}
#endif

class ServerRunGuard
{
public:
    explicit ServerRunGuard(ServerKernel& server)
        : server_(server)
        , thread_([&server] { server.run_until_stopped(); })
    {
    }

    ~ServerRunGuard()
    {
        server_.request_stop();
    }

    void join()
    {
        server_.request_stop();
        thread_.join();
    }

private:
    ServerKernel& server_;
    std::jthread thread_;
};

class StaticRemoteTerminalRuntime final : public IRemoteTerminalRuntime
{
public:
    explicit StaticRemoteTerminalRuntime(
        TerminalSemanticSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    bool ensure_started(std::string&) override
    {
        running_ = true;
        return true;
    }
    bool restart(std::string&) override
    {
        running_ = true;
        return true;
    }
    bool pump() override
    {
        return false;
    }
    RemoteTerminalInputResult send_input(std::string_view) override
    {
        return RemoteTerminalInputResult::Accepted;
    }
    bool resize(int, int) override
    {
        return false;
    }
    bool is_running() const override
    {
        return running_;
    }
    uint64_t process_id() const override
    {
        return 1;
    }
    std::optional<int> exit_code() const override
    {
        return std::nullopt;
    }
    uint64_t scrollback_rows() const override
    {
        return 0;
    }
    std::optional<TerminalSemanticSnapshot> scrollback_page(
        uint64_t, size_t) const override
    {
        return std::nullopt;
    }
    std::optional<std::string> take_clipboard_write() override
    {
        return std::nullopt;
    }
    TerminalSemanticSnapshot snapshot() const override
    {
        return snapshot_;
    }
    TerminalDirtySnapshot take_delta() override
    {
        return {};
    }

private:
    TerminalSemanticSnapshot snapshot_;
    bool running_ = false;
};

} // namespace draxul::tests::server_kernel

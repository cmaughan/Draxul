#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#include <draxul/topology_client.h>

#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
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

RemoteTerminalClient remote_client(
    const std::filesystem::path& runtime,
    std::string client_id,
    std::string epoch = "fixed-epoch",
    std::string method_prefix = "fake")
{
    return RemoteTerminalClient({
        .runtime_directory = runtime,
        .client_id = std::move(client_id),
        .expected_server_epoch = std::move(epoch),
        .method_prefix = std::move(method_prefix),
    });
}

std::string snapshot_text(const TerminalSemanticSnapshot& snapshot)
{
    std::string text;
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
        {
            text += snapshot.cells[
                static_cast<size_t>(row) * snapshot.cols + col]
                        .text;
        }
        text.push_back('\n');
    }
    return text;
}

bool wait_for_text(
    RemoteTerminalClient& client, std::string_view expected,
    std::string& error)
{
    for (int attempt = 0; attempt < 200; ++attempt)
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

#ifdef _WIN32
uint64_t parent_process_id(uint64_t process_id)
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

    ServerRunGuard run_guard(server);
    const auto probe = ServerClient::probe(probe_options(temp.path));
    REQUIRE(probe.ready());
    REQUIRE(probe.welcome->server_pid == server.process_id());
    REQUIRE(probe.welcome->server_epoch == "fixed-epoch");
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "real-remote-terminal")
        != probe.welcome->capabilities.end());
    REQUIRE(std::ranges::find(probe.welcome->capabilities,
                "multi-terminal-v1")
        != probe.welcome->capabilities.end());

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->connected_clients == 1);
    REQUIRE(status.status->terminals == 1);

    ServerKernel duplicate({
        .runtime_directory = temp.path,
    });
    REQUIRE(duplicate.start().disposition
        == ServerStartDisposition::AlreadyRunning);

    std::string shutdown_error;
    REQUIRE(ServerClient::shutdown(temp.path, shutdown_error));
    run_guard.join();
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
    ServerRunGuard run_guard(server);

    REQUIRE(ServerClient::probe(probe_options(temp.path)).state
        == ServerProbeState::Incompatible);
    run_guard.join();
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

TEST_CASE("slow remote observer resyncs without delaying the controller",
    "[server][remote-terminal]")
{
    TempDir temp("draxul-fake-remote-saturation");
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
    for (size_t index = 0; index < kRemoteTerminalQueueLimit + 8; ++index)
    {
        REQUIRE(controller.send_input("x", error));
        REQUIRE(controller.poll(changed, error));
        REQUIRE(changed);
    }

    REQUIRE(observer.poll(changed, error));
    REQUIRE(changed);
    REQUIRE(observer.projection().version()
        == controller.projection().version());
    REQUIRE(terminal_semantic_digest(observer.projection().snapshot())
        == terminal_semantic_digest(controller.projection().snapshot()));

    run_guard.join();
}

TEST_CASE("server-owned shell survives every client detaching and reconnecting",
    "[server][remote-terminal][process]")
{
    TempDir temp("draxul-real-remote-terminal");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto controller
        = remote_client(temp.path, "real-a", "fixed-epoch", "terminal");
    auto observer
        = remote_client(temp.path, "real-b", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(controller.attach(error));
    INFO(error);
    REQUIRE(observer.attach(error));
    INFO(error);

    const uint64_t process_id = controller.projection().pane().process_id;
    const uint64_t generation
        = controller.projection().version().generation;
    REQUIRE(process_id != 0);
    REQUIRE(observer.projection().pane().process_id == process_id);
    REQUIRE(observer.projection().version().generation == generation);
#ifdef _WIN32
    REQUIRE(parent_process_id(process_id) == server.process_id());
    const std::string shared_command
        = "Write-Output '__DRAXUL_SHARED__'\r";
    const std::string delayed_command
        = "Start-Sleep -Milliseconds 250; Write-Output '__DRAXUL_DETACHED__'\r";
#else
    const std::string shared_command
        = "printf '__DRAXUL_SHARED__\\n'\r";
    const std::string delayed_command
        = "sleep 0.25; printf '__DRAXUL_DETACHED__\\n'\r";
#endif

    REQUIRE(controller.send_input(shared_command, error));
    REQUIRE(wait_for_text(observer, "__DRAXUL_SHARED__", error));
    INFO(error);
    REQUIRE(controller.resize(72, 20, error));
    bool resized = false;
    for (int attempt = 0; attempt < 50 && !resized; ++attempt)
    {
        bool changed = false;
        REQUIRE(observer.poll(changed, error));
        resized = observer.projection().snapshot().cols == 72
            && observer.projection().snapshot().rows == 20;
        if (!resized)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(resized);

    REQUIRE(controller.send_input(delayed_command, error));
    REQUIRE(controller.disconnect(error));
    REQUIRE(observer.disconnect(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    auto reconnected
        = remote_client(temp.path, "real-c", "fixed-epoch", "terminal");
    REQUIRE(reconnected.attach(error));
    INFO(error);
    REQUIRE(reconnected.projection().pane().process_id == process_id);
    REQUIRE(reconnected.projection().version().generation == generation);
    REQUIRE(wait_for_text(reconnected, "__DRAXUL_DETACHED__", error));
    INFO(error);

    REQUIRE(reconnected.send_input("exit\r", error));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto after_restart
        = remote_client(temp.path, "real-d", "fixed-epoch", "terminal");
    REQUIRE(after_restart.attach(error));
    REQUIRE(after_restart.projection().version().generation == generation + 1);
    REQUIRE(after_restart.projection().pane().process_id != 0);
    REQUIRE(server.epoch() == "fixed-epoch");

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

    TopologyCommandResult duplicate;
    REQUIRE(first.execute(create, duplicate, error));
    REQUIRE(duplicate.applied);
    REQUIRE(duplicate.duplicate);
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
    REQUIRE(dynamic_terminal.disconnect(error));

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
    REQUIRE(created_tab.name == "PowerShell");
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
    REQUIRE(client.projection().snapshot().metadata.modes.alternate_screen);
    REQUIRE(client.resize(52, 11, error));
    bool changed = false;
    REQUIRE(client.poll(changed, error));
    REQUIRE(client.projection().snapshot().cols == 52);
    REQUIRE(client.projection().snapshot().rows == 11);
    REQUIRE(client.projection().snapshot().metadata.modes.alternate_screen);

    REQUIRE(client.send_input(leave, error));
    for (int attempt = 0; attempt < 200
         && client.projection().snapshot().metadata.modes.alternate_screen;
         ++attempt)
    {
        REQUIRE(client.poll(changed, error));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE_FALSE(
        client.projection().snapshot().metadata.modes.alternate_screen);

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
    for (int index = 0; index < 8; ++index)
    {
        const int cols = index % 2 == 0 ? 240 : 80;
        const int rows = index % 2 == 0 ? 45 : 24;
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

TEST_CASE("remote terminal projection rejects stale identity and sequence",
    "[client][remote-terminal]")
{
    TerminalSemanticSnapshot snapshot{
        .cols = 1,
        .rows = 1,
        .cells = { { .text = "A" } },
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "pane",
            .terminal_id = "terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = 5,
            },
            .snapshot = snapshot,
        },
    };
    RemoteTerminalProjection projection;
    std::string error;
    REQUIRE(projection.attach(attach, error));

    auto event = attach.state;
    event.kind = RemoteTerminalEventKind::Controller;
    event.snapshot.reset();
    event.version.sequence = 7;
    REQUIRE_FALSE(projection.apply(event, error));

    event.version.sequence = 6;
    event.version.server_epoch = "old-epoch";
    REQUIRE_FALSE(projection.apply(event, error));

    event.version.server_epoch = "epoch";
    event.version.generation = 2;
    REQUIRE_FALSE(projection.apply(event, error));
}

TEST_CASE("remote terminal forwards OSC 52 clipboard writes without tracing content",
    "[server][remote-terminal][clipboard]")
{
    TempDir temp("draxul-remote-clipboard");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "clipboard-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client = remote_client(
        temp.path, "clipboard-client", "clipboard-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
#ifdef _WIN32
    const std::string command
        = "[Console]::Write([char]27 + ']52;c;cmVtb3RlIGNsaXBib2FyZA==' + [char]7)\r";
#else
    const std::string command
        = "printf '\\033]52;c;cmVtb3RlIGNsaXBib2FyZA==\\007'\r";
#endif
    REQUIRE(client.send_input(command, error));
    std::optional<std::string> clipboard;
    for (int attempt = 0; attempt < 300 && !clipboard; ++attempt)
    {
        bool changed = false;
        REQUIRE(client.poll(changed, error));
        clipboard = client.take_clipboard_write();
        if (!clipboard)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO("clipboard: "
        << (clipboard ? *clipboard : "<no clipboard event>"));
    REQUIRE(clipboard.has_value());
    REQUIRE(*clipboard == "remote clipboard");

    run_guard.join();
}

TEST_CASE("seeded remote terminal deltas converge or require snapshot resync",
    "[client][remote-terminal][delta][random]")
{
    constexpr int cols = 16;
    constexpr int rows = 8;
    TerminalSemanticSnapshot initial{
        .cols = cols,
        .rows = rows,
        .cells = std::vector<TerminalCellSnapshot>(
            static_cast<size_t>(cols) * rows),
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "pane",
            .terminal_id = "terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = 0,
            },
            .snapshot = initial,
        },
    };
    RemoteTerminalProjection complete;
    RemoteTerminalProjection interrupted;
    std::string error;
    REQUIRE(complete.attach(attach, error));
    REQUIRE(interrupted.attach(attach, error));

    std::mt19937 random(0xD4A5u);
    std::uniform_int_distribution<int> cell_count(1, 20);
    std::uniform_int_distribution<int> col_dist(0, cols - 1);
    std::uniform_int_distribution<int> row_dist(0, rows - 1);
    std::uniform_int_distribution<int> glyph_dist(0, 25);
    bool resync_requested = false;
    for (uint64_t sequence = 1; sequence <= 200; ++sequence)
    {
        TerminalDirtySnapshot delta{
            .cols = cols,
            .rows = rows,
        };
        for (int index = 0; index < cell_count(random); ++index)
        {
            delta.cells.push_back({
                .col = col_dist(random),
                .row = row_dist(random),
                .cell = {
                    .text = std::string(
                        1, static_cast<char>('A' + glyph_dist(random))),
                },
            });
        }
        RemoteTerminalEvent event{
            .kind = RemoteTerminalEventKind::Delta,
            .version = {
                .server_epoch = "epoch",
                .terminal_id = "terminal",
                .generation = 1,
                .sequence = sequence,
            },
            .delta = std::move(delta),
        };
        REQUIRE(complete.apply(event, error));

        if (sequence == 80)
            continue;
        if (sequence == 81)
        {
            REQUIRE_FALSE(interrupted.apply(event, error));
            resync_requested = true;
            RemoteTerminalEvent snapshot{
                .kind = RemoteTerminalEventKind::Snapshot,
                .version = event.version,
                .snapshot = complete.snapshot(),
            };
            REQUIRE(interrupted.apply(snapshot, error));
            continue;
        }
        REQUIRE(interrupted.apply(event, error));
        REQUIRE(terminal_semantic_digest(interrupted.snapshot())
            == terminal_semantic_digest(complete.snapshot()));
    }
    REQUIRE(resync_requested);
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
    REQUIRE(status.status->terminals == 1);
    REQUIRE(status.status->sessions == 1);
    REQUIRE(status.status->spaces == 1);

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

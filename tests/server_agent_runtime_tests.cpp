#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

TEST_CASE("server-owned shell discovery converges in two agent clients",
    "[server][agent][process]")
{
    TempDir temp("draxul-server-agent");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto terminal = remote_client(
        temp.path, "agent-terminal", "fixed-epoch",
        "terminal");
    std::string error;
    REQUIRE(terminal.attach(error));
    INFO(error);

#ifdef _WIN32
    const auto source
        = std::filesystem::path(
              std::getenv("SystemRoot"))
        / "System32" / "cmd.exe";
    const auto fake_agent = temp.path / "codex.exe";
    std::filesystem::copy_file(source, fake_agent);
    const std::string command
        = "Write-Output ('__CODEX_' + 'TEST_STARTED__'); "
          "Start-Process -FilePath '"
        + fake_agent.string()
        + "' -ArgumentList '/Q','/C',"
          "'ping -t 127.0.0.1 >nul' -NoNewWindow\r";
#else
    const auto fake_agent = temp.path / "codex";
    std::filesystem::create_symlink(
        "/bin/sleep", fake_agent);
    const std::string command = "'"
        + fake_agent.string() + "' 30\r";
#endif
    REQUIRE(terminal.send_input(command, error));
#ifdef _WIN32
    REQUIRE(wait_for_text(
        terminal, "__CODEX_TEST_STARTED__", error));
#endif

    AgentClient first({
        .runtime_directory = temp.path,
        .client_id = "agent-a",
    });
    AgentClient second({
        .runtime_directory = temp.path,
        .client_id = "agent-b",
    });
    REQUIRE(first.refresh(error));
    REQUIRE(second.refresh(error));
    const bool discovered_agent
        = wait_for_agent(first, "codex", error);
    INFO("terminal screen:\n"
        << snapshot_text(terminal.projection().snapshot()));
    REQUIRE(discovered_agent);
    INFO(error);
    bool changed = false;
    for (int attempt = 0;
        attempt < 100
        && second.snapshot() != first.snapshot();
        ++attempt)
    {
        REQUIRE(second.poll(changed, error));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    }
    REQUIRE(second.snapshot() == first.snapshot());
    REQUIRE(first.snapshot().agents.size() == 1);
    CHECK(first.snapshot().agents[0].pane_id
        == kServerShellPaneId);
    CHECK(first.snapshot().agents[0].identity.origin
        == AgentIdentityOrigin::Discovered);

    const std::string instance_id
        = first.snapshot().agents[0].identity.instance_id;
    REQUIRE(terminal.disconnect(error));
    const auto server_request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    const auto listed = server_request(
        "agent.list", nlohmann::json::object());
    REQUIRE(listed.ok);
    REQUIRE(listed.result.size() == 1);
    CHECK(listed.result[0]["instance_id"] == instance_id);

    const auto waited = server_request("agent.wait",
        {
            { "instance_id", instance_id },
            { "until", { "running" } },
        });
    REQUIRE(waited.ok);
    CHECK(waited.result["complete"].get<bool>());

    const auto read = server_request("pane.read",
        {
            { "pane_id", kServerShellPaneId },
            { "lines", 20 },
        });
    REQUIRE(read.ok);
    CHECK(read.result["lines"].is_array());

    const auto sent = server_request("agent.send_text",
        {
            { "instance_id", instance_id },
            { "text", "x" },
        });
    REQUIRE(sent.ok);
    const auto keyed = server_request("agent.send_keys",
        {
            { "instance_id", instance_id },
            { "keys", { "tab" } },
        });
    REQUIRE(keyed.ok);

    const auto restarted = server_request("agent.restart",
        { { "instance_id", instance_id } });
    REQUIRE(restarted.ok);
    CHECK(restarted.result["accepted"].get<bool>());
    run_guard.join();
}

TEST_CASE("managed agents launch and restart without a UI",
    "[server][agent][managed][process][pane-move]")
{
    TempDir temp("draxul-server-managed-agent");
    AgentDefinition test_agent{
        .profile_id = "test-managed",
        .kind = "codex",
        .display_name = "Managed Codex",
#ifdef _WIN32
        .executable = "powershell.exe",
        .default_args = {
            "-NoLogo",
            "-NoProfile",
            "-Command",
            "Write-Output ('__DRAXUL_AGENT_ENV__' + "
            "$env:DRAXUL_SERVER_EPOCH + ':' + "
            "$env:DRAXUL_RUNTIME_GENERATION); "
            "while ($true) { Start-Sleep -Seconds 1 }",
        },
#else
        .executable = "/bin/sh",
        .default_args = {
            "-c",
            "echo \"__DRAXUL_AGENT_ENV__"
            "$DRAXUL_SERVER_EPOCH:$DRAXUL_RUNTIME_GENERATION\"; "
            "while true; do sleep 1; done",
        },
#endif
    };
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "managed-epoch",
        .agent_definitions = { test_agent },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    const auto request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    const auto started = request("agent.start",
        {
            { "profile_id", "test-managed" },
            { "client_id", "managed-agent-a" },
            { "request_id", "managed-start-1" },
            { "cwd", temp.path.string() },
            { "args", nlohmann::json::array() },
        });
    INFO(started.error_code << ": "
                            << started.error_message);
    REQUIRE(started.ok);
    const auto replayed_start = request("agent.start",
        {
            { "profile_id", "test-managed" },
            { "client_id", "managed-agent-a" },
            { "request_id", "managed-start-1" },
            { "cwd", temp.path.string() },
            { "args", nlohmann::json::array() },
        });
    REQUIRE(replayed_start.ok);
    CHECK(replayed_start.result["instance_id"]
        == started.result["instance_id"]);
    CHECK(started.result["origin"] == "managed");
    CHECK(started.result["running"].get<bool>());
    CHECK(started.result["runtime_generation"] == 1);
    const std::string instance_id
        = started.result["instance_id"].get<std::string>();
    const std::string pane_id
        = started.result["route"]["pane_id"]
              .get<std::string>();
    const std::string terminal_id
        = started.result["route"]["terminal_id"]
              .get<std::string>();

    RemoteTerminalClient observer({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-b",
        .expected_server_epoch = "managed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    std::string terminal_error;
    REQUIRE(observer.attach(terminal_error));
    INFO(terminal_error);
    CHECK(observer.projection()
            .controller_client_id()
            .empty());
    CHECK_FALSE(observer.send_input("x", terminal_error));
    CHECK(observer.last_error_code()
        == "not_controller");

    RemoteTerminalClient launcher({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-a",
        .expected_server_epoch = "managed-epoch",
        .method_prefix = "terminal",
        .terminal_id = terminal_id,
    });
    REQUIRE(launcher.attach(terminal_error));
    INFO(terminal_error);
    CHECK(launcher.projection()
              .controller_client_id()
        == "managed-agent-a");
    bool controller_changed = false;
    REQUIRE(observer.poll(
        controller_changed, terminal_error));
    CHECK(controller_changed);
    CHECK(observer.projection()
              .controller_client_id()
        == "managed-agent-a");
    REQUIRE(launcher.send_input("x", terminal_error));

    AgentClient first({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-a",
    });
    AgentClient second({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-b",
    });
    std::string agent_error;
    REQUIRE(first.refresh(agent_error));
    REQUIRE(second.refresh(agent_error));
    REQUIRE(first.snapshot() == second.snapshot());
    REQUIRE(first.snapshot().agents.size() == 1);
    CHECK(first.snapshot().agents[0].identity.instance_id == instance_id);

    auto wait_for_environment
        = [&](std::string_view expected) {
              for (int attempt = 0; attempt < 200; ++attempt)
              {
                  const auto read = request("pane.read",
                      {
                          { "pane_id", pane_id },
                          { "lines", 24 },
                      });
                  if (read.ok)
                  {
                      for (const auto& line : read.result["lines"])
                      {
                          if (line.get<std::string>()
                                  .find(expected)
                              != std::string::npos)
                          {
                              return true;
                          }
                      }
                  }
                  std::this_thread::sleep_for(
                      std::chrono::milliseconds(25));
              }
              return false;
          };
    REQUIRE(wait_for_environment(
        "__DRAXUL_AGENT_ENV__managed-epoch:1"));

    const auto reported = request(
        "pane.report_agent_session",
        {
            { "server_epoch", "managed-epoch" },
            { "runtime_generation", 1 },
            { "pane_id", pane_id },
            { "agent_instance_id", instance_id },
            { "source", "draxul:codex" },
            { "agent", "codex" },
            { "integration_version", 2 },
            { "sequence", 1 },
            { "ref_kind", "id" },
            { "ref_value", "managed-native-session" },
        });
    REQUIRE(reported.ok);

    TopologyClient topology_controller({
        .runtime_directory = temp.path,
        .client_id = "managed-agent-route-controller",
    });
    REQUIRE(topology_controller.refresh(agent_error));
    const TopologySpace agent_source_space
        = topology_controller.snapshot().spaces.front();
    const TopologyTab agent_source_tab
        = agent_source_space.tabs.front();
    TopologyCommand create_agent_destination{
        .command_id = "managed-agent-destination",
        .expected_revision
        = topology_controller.snapshot().revision,
        .kind = TopologyCommandKind::CreateTab,
        .space_id = agent_source_space.space_id,
        .name = "Managed agent destination",
        .pane_domain = TopologyPaneDomain::ServerTerminal,
    };
    TopologyCommandResult agent_destination_created;
    REQUIRE(topology_controller.execute(
        create_agent_destination,
        agent_destination_created, agent_error));
    const TopologyTab agent_destination_tab
        = agent_destination_created.snapshot.spaces.front()
              .tabs.back();
    TopologyCommand move_agent{
        .command_id = "move-managed-agent",
        .expected_revision
        = topology_controller.snapshot().revision,
        .kind = TopologyCommandKind::MovePane,
        .space_id = agent_source_space.space_id,
        .tab_id = agent_source_tab.tab_id,
        .destination_space_id = agent_source_space.space_id,
        .destination_tab_id = agent_destination_tab.tab_id,
        .pane_id = pane_id,
        .target_pane_id
        = agent_destination_tab.panes.front().pane_id,
    };
    TopologyCommandResult agent_moved;
    REQUIRE(topology_controller.execute(
        move_agent, agent_moved, agent_error));
    const auto& moved_agent_space
        = agent_moved.snapshot.spaces.front();
    const auto moved_agent_destination
        = std::ranges::find(moved_agent_space.tabs,
            agent_destination_tab.tab_id,
            &TopologyTab::tab_id);
    REQUIRE(moved_agent_destination
        != moved_agent_space.tabs.end());
    const auto moved_agent_pane
        = std::ranges::find(moved_agent_destination->panes,
            pane_id, &TopologyPane::pane_id);
    REQUIRE(moved_agent_pane
        != moved_agent_destination->panes.end());
    CHECK(moved_agent_pane->terminal_id == terminal_id);
    REQUIRE(moved_agent_pane->agent);
    CHECK(moved_agent_pane->agent->instance_id == instance_id);

    ControlClientResult moved_agent;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        moved_agent = request(
            "agent.get", { { "instance_id", instance_id } });
        if (moved_agent.ok
            && moved_agent.result.is_object()
            && moved_agent.result.contains("route")
            && moved_agent.result.at("route").is_object()
            && moved_agent.result.at("route").value(
                   "tab_id", "")
                == agent_destination_tab.tab_id)
        {
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(moved_agent.ok);
    REQUIRE(moved_agent.result.is_object());
    REQUIRE(moved_agent.result.contains("instance_id"));
    REQUIRE(moved_agent.result.at("instance_id").is_string());
    REQUIRE(moved_agent.result.contains("runtime_generation"));
    REQUIRE(moved_agent.result.at("runtime_generation")
            .is_number_unsigned());
    REQUIRE(moved_agent.result.contains("route"));
    REQUIRE(moved_agent.result.at("route").is_object());
    const auto& moved_route = moved_agent.result.at("route");
    REQUIRE(moved_route.contains("space_id"));
    REQUIRE(moved_route.contains("tab_id"));
    REQUIRE(moved_route.contains("pane_id"));
    REQUIRE(moved_route.contains("terminal_id"));
    CHECK(moved_agent.result.at("instance_id") == instance_id);
    CHECK(moved_agent.result.at("runtime_generation") == 1);
    CHECK(moved_route.at("space_id")
        == agent_source_space.space_id);
    CHECK(moved_route.at("tab_id")
        == agent_destination_tab.tab_id);
    CHECK(moved_route.at("pane_id") == pane_id);
    CHECK(moved_route.at("terminal_id") == terminal_id);

    const auto sent_after_move = request("agent.send_text",
        {
            { "instance_id", instance_id },
            { "text", "x" },
        });
    REQUIRE(sent_after_move.ok);
    REQUIRE(sent_after_move.result.is_object());
    REQUIRE(sent_after_move.result.contains("route"));
    REQUIRE(sent_after_move.result.at("route").is_object());
    REQUIRE(sent_after_move.result.at("route").contains(
        "tab_id"));
    // Input mutations return the refreshed agent projection, including its
    // authoritative route, rather than a separate `accepted` flag.
    CHECK(sent_after_move.result.at("route").at("tab_id")
        == agent_destination_tab.tab_id);

    const auto moved_report = request(
        "pane.report_agent_session",
        {
            { "server_epoch", "managed-epoch" },
            { "runtime_generation", 1 },
            { "pane_id", pane_id },
            { "agent_instance_id", instance_id },
            { "source", "draxul:codex" },
            { "agent", "codex" },
            { "integration_version", 2 },
            { "sequence", 2 },
            { "ref_kind", "id" },
            { "ref_value", "managed-native-session-moved" },
        });
    REQUIRE(moved_report.ok);
    const auto routed_agent = request(
        "agent.get", { { "instance_id", instance_id } });
    REQUIRE(routed_agent.ok);
    REQUIRE(routed_agent.result.is_object());
    REQUIRE(routed_agent.result.contains("route"));
    REQUIRE(routed_agent.result.at("route").is_object());
    REQUIRE(routed_agent.result.at("route").contains(
        "tab_id"));
    REQUIRE(routed_agent.result.contains("session_ref"));
    REQUIRE(routed_agent.result.at("session_ref").is_object());
    const auto& routed_session
        = routed_agent.result.at("session_ref");
    REQUIRE(routed_session.contains("sequence"));
    REQUIRE(routed_session.contains("value"));
    CHECK(routed_agent.result.at("route").at("tab_id")
        == agent_destination_tab.tab_id);
    CHECK(routed_session.at("sequence") == 2);
    CHECK(routed_session.at("value")
        == "managed-native-session-moved");
    const auto waited_after_move = request("agent.wait",
        {
            { "instance_id", instance_id },
            { "until", { "running" } },
        });
    REQUIRE(waited_after_move.ok);
    REQUIRE(waited_after_move.result.is_object());
    REQUIRE(waited_after_move.result.contains("complete"));
    REQUIRE(waited_after_move.result.at("complete").is_boolean());
    CHECK(waited_after_move.result.at("complete").get<bool>());

    const auto restarted = request(
        "agent.restart",
        {
            { "instance_id", instance_id },
            { "request_id", "managed-restart-1" },
        });
    REQUIRE(restarted.ok);
    CHECK(restarted.result["runtime_generation"] == 2);
    const auto replayed_restart = request(
        "agent.restart",
        {
            { "instance_id", instance_id },
            { "request_id", "managed-restart-1" },
        });
    REQUIRE(replayed_restart.ok);
    CHECK(replayed_restart.result["runtime_generation"] == 2);
    REQUIRE(wait_for_environment(
        "__DRAXUL_AGENT_ENV__managed-epoch:2"));

    const auto stale_report = request(
        "pane.report_agent_session",
        {
            { "server_epoch", "managed-epoch" },
            { "runtime_generation", 1 },
            { "pane_id", pane_id },
            { "agent_instance_id", instance_id },
            { "source", "draxul:codex" },
            { "agent", "codex" },
            { "integration_version", 2 },
            { "sequence", 2 },
            { "ref_kind", "id" },
            { "ref_value", "stale-native-session" },
        });
    CHECK_FALSE(stale_report.ok);
    CHECK(stale_report.error_code == "agent_replaced");

    const auto topology = request(
        "topology.snapshot", nlohmann::json::object());
    REQUIRE(topology.ok);
    REQUIRE(topology.result.is_object());
    REQUIRE(topology.result.contains("spaces"));
    REQUIRE(topology.result.at("spaces").is_array());
    REQUIRE_FALSE(topology.result.at("spaces").empty());
    const auto& topology_space
        = topology.result.at("spaces").front();
    REQUIRE(topology_space.is_object());
    REQUIRE(topology_space.contains("tabs"));
    REQUIRE(topology_space.at("tabs").is_array());
    const nlohmann::json* topology_agent = nullptr;
    for (const auto& tab : topology_space.at("tabs"))
    {
        REQUIRE(tab.is_object());
        REQUIRE(tab.contains("panes"));
        REQUIRE(tab.at("panes").is_array());
        for (const auto& pane : tab.at("panes"))
        {
            REQUIRE(pane.is_object());
            if (pane.value("pane_id", "") == pane_id)
                topology_agent = &pane;
        }
    }
    REQUIRE(topology_agent);
    REQUIRE(topology_agent->contains("agent"));
    REQUIRE(topology_agent->at("agent").is_object());
    REQUIRE(topology_agent->at("agent").contains(
        "instance_id"));
    REQUIRE(topology_agent->contains(
        "server_working_directory"));
    CHECK(topology_agent->at("agent").at("instance_id")
        == instance_id);
    CHECK(topology_agent->at("server_working_directory")
        == temp.path.string());
    run_guard.join();

    std::string load_error;
    const auto saved = load_session_state_from_path(
        server_session_state_path(temp.path),
        &load_error);
    INFO(load_error);
    REQUIRE(saved);
    const SessionPaneSnapshot* saved_agent = nullptr;
    for (const auto& tab : saved->spaces.front().tabs)
    {
        const auto found = std::ranges::find(
            tab.pane_layout.panes, pane_id,
            &SessionPaneSnapshot::pane_id);
        if (found != tab.pane_layout.panes.end())
            saved_agent = &*found;
    }
    REQUIRE(saved_agent);
    REQUIRE(saved_agent->agent);
    CHECK(saved_agent->agent->instance_id
        == instance_id);
    CHECK(saved_agent->launch.working_dir
        == temp.path.string());

    ServerKernel restored({
        .runtime_directory = temp.path,
        .epoch_override = "managed-restored",
        .agent_definitions = { test_agent },
    });
    REQUIRE(restored.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard restored_guard(restored);
    const auto restored_request
        = [&](std::string_view method,
              nlohmann::json params) {
              params["session_id"] = "default";
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, temp.path),
                  temp.path, method, std::move(params));
          };
    ControlClientResult restored_agent;
    for (int attempt = 0;
        attempt < 200 && !restored_agent.ok;
        ++attempt)
    {
        restored_agent = restored_request(
            "agent.get",
            { { "instance_id", instance_id } });
        if (!restored_agent.ok)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    REQUIRE(restored_agent.ok);
    CHECK(restored_agent.result["running"].get<bool>());
    CHECK(restored_agent.result["runtime_generation"] == 1);
    const std::string restored_pane_id
        = restored_agent.result["route"]["pane_id"]
              .get<std::string>();
    REQUIRE_FALSE(restored_pane_id.empty());
    CHECK(restored_pane_id != pane_id);

    bool restored_environment = false;
    for (int attempt = 0;
        attempt < 200 && !restored_environment;
        ++attempt)
    {
        const auto read = restored_request(
            "pane.read",
            {
                { "pane_id", restored_pane_id },
                { "lines", 24 },
            });
        if (read.ok)
        {
            for (const auto& line : read.result["lines"])
            {
                restored_environment
                    = restored_environment
                    || line.get<std::string>().find(
                           "__DRAXUL_AGENT_ENV__"
                           "managed-restored:1")
                        != std::string::npos;
            }
        }
        if (!restored_environment)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    CHECK(restored_environment);
    restored_guard.join();
}

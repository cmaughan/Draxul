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
    REQUIRE(wait_for_text(terminal, "PS ", error));
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
    "[server][agent][managed][process]")
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
    const auto& panes = topology.result["spaces"][0]
                                       ["tabs"][0]["panes"];
    REQUIRE(panes.size() == 2);
    CHECK(panes[1]["agent"]["instance_id"]
        == instance_id);
    CHECK(panes[1]["server_working_directory"]
        == temp.path.string());
    run_guard.join();

    std::string load_error;
    const auto saved = load_session_state_from_path(
        server_session_state_path(temp.path),
        &load_error);
    INFO(load_error);
    REQUIRE(saved);
    const auto& saved_panes
        = saved->spaces.front().tabs.front().pane_layout.panes;
    const auto saved_agent = std::ranges::find(
        saved_panes, pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(saved_agent != saved_panes.end());
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

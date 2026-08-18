#include <catch2/catch_test_macros.hpp>

#include <draxul/agent_protocol.h>
#include "server_agent_service.h"

#include <nlohmann/json.hpp>

using namespace draxul;

namespace
{

ServerAgentRuntimeView discovered_codex_runtime(
    std::chrono::steady_clock::time_point now)
{
    return {
        .space_id = "space-1",
        .tab_id = "tab-1",
        .pane_id = "pane-1",
        .terminal_id = "terminal-1",
        .generation = { 4 },
        .runtime_running = true,
        .process_observation = AgentProcessObservation{
            .captured_at = now,
            .processes = { {
                .process_id = 42,
                .parent_process_id = 41,
                .executable = "C:/tools/codex.exe",
            } },
            .foreground_reliable = false,
        },
        .terminal_observation = AgentObservation{
            .output_generation = 3,
            .captured_at = now,
            .bottom_rows = { "Enter a prompt" },
            .process_running = true,
        },
    };
}

} // namespace

TEST_CASE("server agent snapshots round-trip sanitized state",
    "[agent][protocol]")
{
    const ServerAgentSnapshot snapshot{
        .revision = 7,
        .session_id = "work",
        .agents = { {
            .space_id = "space-1",
            .tab_id = "tab-1",
            .pane_id = "pane-1",
            .terminal_id = "terminal-1",
            .identity = {
                .kind = "codex",
                .display_name = "Codex",
                .instance_id = "agent-1",
                .origin = AgentIdentityOrigin::Discovered,
            },
            .identity_evidence_category = "direct_executable",
            .identity_high_confidence = true,
            .lifecycle = AgentLifecycle::Running,
            .generation = { 3 },
            .status = AgentStatus::Working,
            .status_authority
            = AgentStateAuthority::ScreenManifest,
            .manifest_id = "codex-screen",
            .manifest_version = 1,
            .rule_id = "working",
            .status_evidence_category = "spinner",
            .observation_generation = 12,
            .attention = false,
            .running = true,
        } },
    };

    const nlohmann::json encoded
        = server_agent_snapshot_to_json(snapshot);
    const std::string wire = encoded.dump();
    CHECK(wire.find("Enter a prompt") == std::string::npos);
    CHECK(wire.find("bottom_rows") == std::string::npos);
    CHECK(wire.find("processes") == std::string::npos);

    std::string error;
    const auto decoded
        = server_agent_snapshot_from_json(encoded, error);
    INFO(error);
    REQUIRE(decoded);
    CHECK(*decoded == snapshot);

    auto duplicate = encoded;
    duplicate["agents"].push_back(duplicate["agents"][0]);
    CHECK_FALSE(server_agent_snapshot_from_json(
        duplicate, error));

    auto oversized_manifest = encoded;
    oversized_manifest["agents"][0]["manifest_version"]
        = uint64_t{ 4'294'967'297 };
    CHECK_FALSE(server_agent_snapshot_from_json(
        oversized_manifest, error));

    auto oversized_exit = encoded;
    oversized_exit["agents"][0]["exit_code"]
        = uint64_t{ 4'294'967'297 };
    CHECK_FALSE(server_agent_snapshot_from_json(
        oversized_exit, error));

    auto negative_revision = encoded;
    negative_revision["revision"] = -1;
    CHECK_FALSE(server_agent_snapshot_from_json(
        negative_revision, error));
}

TEST_CASE("server agent service discovers evaluates and retires a runtime",
    "[agent][server]")
{
    ServerAgentService service("work");
    const auto now = std::chrono::steady_clock::now();
    auto runtime = discovered_codex_runtime(now);

    service.update({ runtime }, now);
    REQUIRE(service.snapshot().agents.size() == 1);
    const std::string instance_id
        = service.snapshot().agents[0].identity.instance_id;
    CHECK(instance_id.starts_with(
        "server-discovered-terminal-1-4-"));
    CHECK(service.snapshot().agents[0].status
        == AgentStatus::Unknown);

    runtime.terminal_observation->captured_at
        = now + std::chrono::seconds(4);
    service.update(
        { runtime }, now + std::chrono::seconds(4));
    REQUIRE(service.snapshot().agents.size() == 1);
    CHECK(service.snapshot().agents[0].identity.instance_id
        == instance_id);
    CHECK(service.snapshot().agents[0].status
        == AgentStatus::Idle);

    const auto listed = service.handle(
        "agent.list", nlohmann::json::object());
    REQUIRE(listed.ok);
    REQUIRE(listed.value.size() == 1);
    CHECK(listed.value[0]["instance_id"] == instance_id);
    CHECK(listed.value[0]["route"]["terminal_id"]
        == "terminal-1");

    const auto fetched = service.handle(
        "agent.get", { { "instance_id", instance_id } });
    REQUIRE(fetched.ok);
    CHECK(fetched.value["runtime_generation"] == 4);

    const auto explained = service.handle(
        "agent.explain", { { "instance_id", instance_id } });
    REQUIRE(explained.ok);
    CHECK(explained.value["identity_explanation"]
        ["evidence_category"] == "direct_executable");

    const auto waiting = service.handle("agent.wait",
        {
            { "instance_id", instance_id },
            { "until", { "working" } },
            { "runtime_generation", 4 },
        });
    REQUIRE(waiting.ok);
    CHECK_FALSE(waiting.value["complete"].get<bool>());

    nlohmann::json excessive_wait_states
        = nlohmann::json::array();
    for (size_t index = 0;
         index <= kServerAgentMaxWaitStates; ++index)
    {
        excessive_wait_states.push_back("working");
    }
    const auto too_many_wait_states = service.handle(
        "agent.wait",
        {
            { "instance_id", instance_id },
            { "until", excessive_wait_states },
        });
    CHECK_FALSE(too_many_wait_states.ok);
    CHECK(too_many_wait_states.error_code == "invalid_params");
    const auto oversized_wait_state = service.handle(
        "agent.wait",
        {
            { "instance_id", instance_id },
            { "until", {
                  std::string(
                      kServerAgentMaxWaitStateBytes + 1, 'x') } },
        });
    CHECK_FALSE(oversized_wait_state.ok);
    CHECK(oversized_wait_state.error_code == "invalid_params");

    const auto replaced = service.handle("agent.wait",
        {
            { "instance_id", instance_id },
            { "runtime_generation", 3 },
        });
    REQUIRE(replaced.ok);
    CHECK(replaced.value["complete"].get<bool>());
    CHECK(replaced.value["outcome"] == "agent_replaced");

    const uint64_t revision = service.snapshot().revision;
    const auto unchanged = service.handle("agent.poll",
        { { "after_revision", revision } });
    REQUIRE(unchanged.ok);
    CHECK_FALSE(unchanged.value["changed"].get<bool>());

    runtime.process_observation.reset();
    runtime.terminal_observation.reset();
    for (int probe = 0; probe < 6; ++probe)
    {
        service.update({ runtime },
            now + std::chrono::seconds(5 + probe));
    }
    CHECK(service.snapshot().agents.empty());

    const auto changed = service.handle("agent.poll",
        { { "after_revision", revision } });
    REQUIRE(changed.ok);
    CHECK(changed.value["changed"].get<bool>());
    CHECK(changed.value["snapshot"]["agents"].empty());
}

TEST_CASE("agent key encoding is shared by UI and server control",
    "[agent][input]")
{
    std::string error;
    const auto encoded = encode_agent_keys(
        { "Enter", "CTRL+C", "left", "f12" }, error);
    INFO(error);
    REQUIRE(encoded);
    CHECK(*encoded == "\r\x03\x1b[D\x1b[24~");

    const auto unsupported = encode_agent_keys(
        { "ctrl+shift+x" }, error);
    CHECK_FALSE(unsupported);
    CHECK(error == "Unsupported key: ctrl+shift+x");
}

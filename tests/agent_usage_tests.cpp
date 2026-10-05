#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "server_agent_service.h"
#include "support/temp_dir.h"

#include <draxul/agent_protocol.h>
#include <draxul/agent_usage.h>

#include <cstdio>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace draxul;
using draxul::tests::TempDir;

namespace
{

UsageTimestamp usage_now()
{
    return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
}

std::string iso(UsageTimestamp time)
{
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(time);
    const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &utc);
    const auto millis = (time - seconds).count();
    char result[48]{};
    std::snprintf(result, sizeof(result), "%s.%03dZ", text, static_cast<int>(millis));
    return result;
}

void append(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::app) << text;
}

// One streamed Claude line; Claude repeats a message's usage on every content
// block, growing output_tokens as it streams.
std::string claude_line(UsageTimestamp time, const std::string& message_id,
    const std::string& cwd, uint64_t input, uint64_t cache_read, uint64_t output)
{
    return nlohmann::json{
        { "type", "assistant" },
        { "timestamp", iso(time) },
        { "cwd", cwd },
        { "sessionId", "claude-session" },
        { "message", { { "id", message_id }, { "model", "claude-opus-5-5" },
                         { "usage", { { "input_tokens", input }, { "cache_creation_input_tokens", 0 },
                                        { "cache_read_input_tokens", cache_read },
                                        { "output_tokens", output } } } } },
    }.dump()
        + "\n";
}

std::string codex_meta(UsageTimestamp time, const std::string& id, const std::string& cwd)
{
    return nlohmann::json{ { "timestamp", iso(time) }, { "type", "session_meta" },
               { "payload", { { "id", id }, { "cwd", cwd } } } }
               .dump()
        + "\n";
}

std::string codex_tokens(UsageTimestamp time, uint64_t total_input, uint64_t total_output,
    uint64_t last_input, uint64_t last_output)
{
    return nlohmann::json{
        { "timestamp", iso(time) },
        { "type", "event_msg" },
        { "payload", { { "type", "token_count" },
                         { "info", { { "total_token_usage", { { "input_tokens", total_input }, { "output_tokens", total_output } } },
                                       { "last_token_usage", { { "input_tokens", last_input }, { "output_tokens", last_output } } } } } } },
    }.dump()
        + "\n";
}

std::filesystem::path codex_day(const std::filesystem::path& root, UsageTimestamp time)
{
    const std::time_t raw = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif
    char text[16]{};
    std::strftime(text, sizeof(text), "%Y/%m/%d", &local);
    return root / text;
}

} // namespace

TEST_CASE("Agent usage attributes each agent to its own native session and measures its token rate",
    "[agent-usage][server]")
{
    TempDir temp("dx-usage");
    const AgentUsageRoots roots{ temp.path / "claude" / "projects", temp.path / "codex" / "sessions" };
    AgentUsageMonitor monitor(roots, { .native_watch = false });
    const auto now = usage_now();
    const auto started = now - std::chrono::minutes{ 5 };
    const std::string alpha = (temp.path / "alpha").string();
    const std::string beta = (temp.path / "beta").string();
    const std::string shared = (temp.path / "shared").string();
    CHECK(claude_project_directory_name("/Users/me/dev/my_app.v2") == "-Users-me-dev-my-app-v2");

    const auto alpha_file = roots.claude_projects / claude_project_directory_name(alpha) / "a.jsonl";
    // Streaming repeats message m1's usage; only its growth counts.
    append(alpha_file, claude_line(now - std::chrono::seconds{ 30 }, "m1", alpha, 1000, 9000, 10));
    append(alpha_file, claude_line(now - std::chrono::seconds{ 29 }, "m1", alpha, 1000, 9000, 200));
    append(alpha_file, claude_line(now - std::chrono::seconds{ 20 }, "m2", alpha, 500, 9500, 100));
    // A session that ended long before the agent started must not be chosen.
    const auto stale_file = roots.claude_projects / claude_project_directory_name(alpha) / "old.jsonl";
    append(stale_file, claude_line(now - std::chrono::hours{ 2 }, "old", alpha, 1, 1, 1));
    std::filesystem::last_write_time(stale_file,
        std::filesystem::last_write_time(stale_file) - std::chrono::hours{ 2 });

    const auto beta_file = roots.claude_projects / claude_project_directory_name(beta) / "b.jsonl";
    append(beta_file, claude_line(now - std::chrono::seconds{ 50 }, "b1", beta, 10, 0, 10));
    append(beta_file, claude_line(now - std::chrono::seconds{ 40 }, "b2", beta, 10, 0, 10));

    const auto shared_file = roots.claude_projects / claude_project_directory_name(shared) / "s.jsonl";
    append(shared_file, claude_line(now - std::chrono::seconds{ 10 }, "s1", shared, 10, 0, 10));

    const auto codex_file = codex_day(roots.codex_sessions, now) / "rollout-x-019f-codex.jsonl";
    append(codex_file, codex_meta(now - std::chrono::seconds{ 60 }, "019f-codex", "/elsewhere"));
    append(codex_file, codex_tokens(now - std::chrono::seconds{ 30 }, 4000, 100, 4000, 100));
    // Codex repeats a cumulative total; the duplicate must not double count.
    append(codex_file, codex_tokens(now - std::chrono::seconds{ 29 }, 4000, 100, 4000, 100));
    append(codex_file, codex_tokens(now - std::chrono::seconds{ 10 }, 9000, 300, 5000, 200));

    const std::vector<AgentUsageRequest> requests{
        { .instance_id = "alpha", .kind = "claude", .working_directory = alpha, .started_at = started },
        { .instance_id = "beta", .kind = "claude", .working_directory = beta + "/", .started_at = started },
        { .instance_id = "shared-1", .kind = "claude", .working_directory = shared, .started_at = started },
        { .instance_id = "shared-2", .kind = "claude", .working_directory = shared, .started_at = started },
        { .instance_id = "codex",
            .kind = "codex",
            .session_ref = AgentSessionRef{ .source = "draxul:codex", .agent_kind = "codex",
                .kind = AgentSessionRefKind::Id, .value = "019f-codex" },
            .working_directory = alpha,
            .started_at = started },
    };
    auto activity = monitor.update(requests, now);

    REQUIRE(activity.contains("alpha"));
    // m1 peaks at 1000 + 9000 + 200, m2 adds 500 + 9500 + 100.
    CHECK(activity["alpha"].session_tokens == 10'200 + 10'100);
    // TokenFu's window: the first record anchors, m1's growth lands at -29s
    // and m2 at -20s, so 10'100 + 190 tokens over 10 seconds.
    CHECK(activity["alpha"].tokens_per_second == Catch::Approx((10'100.0 + 190.0) / 10.0));
    CHECK(activity["alpha"].measured_at_ms
        == (now - std::chrono::seconds{ 20 }).time_since_epoch().count());

    REQUIRE(activity.contains("beta"));
    CHECK(activity["beta"].session_tokens == 40);
    CHECK(activity["beta"].tokens_per_second == Catch::Approx(2.0));

    // Two agents of one kind in one directory cannot be told apart.
    CHECK_FALSE(activity.contains("shared-1"));
    CHECK_FALSE(activity.contains("shared-2"));

    // An explicit native session id wins over the working directory.
    REQUIRE(activity.contains("codex"));
    CHECK(activity["codex"].session_tokens == 4100 + 5200);
    CHECK(activity["codex"].tokens_per_second == Catch::Approx(5200.0 / 20.0));

    // Appended output is read incrementally on the next update.
    const auto later = now + std::chrono::seconds{ 5 };
    append(alpha_file, claude_line(later - std::chrono::seconds{ 1 }, "m3", alpha, 100, 0, 100));
    activity = monitor.update(requests, later);
    CHECK(activity["alpha"].session_tokens == 10'200 + 10'100 + 200);
    CHECK(activity["alpha"].measured_at_ms
        == (later - std::chrono::seconds{ 1 }).time_since_epoch().count());

    // Agents that leave release their bindings.
    CHECK(monitor.update({}, later).empty());
}

TEST_CASE("Agent usage finds an unreferenced Codex session by its recorded directory",
    "[agent-usage][server]")
{
    TempDir temp("dx-usage-codex");
    const AgentUsageRoots roots{ temp.path / "claude" / "projects", temp.path / "codex" / "sessions" };
    AgentUsageMonitor monitor(roots, { .native_watch = false });
    const auto now = usage_now();
    const std::string project = (temp.path / "project").string();
    const auto day = codex_day(roots.codex_sessions, now);
    append(day / "rollout-a-other.jsonl", codex_meta(now, "other", (temp.path / "other").string()));
    append(day / "rollout-b-mine.jsonl", codex_meta(now - std::chrono::seconds{ 30 }, "mine", project));
    append(day / "rollout-b-mine.jsonl", codex_tokens(now - std::chrono::seconds{ 20 }, 100, 10, 100, 10));
    append(day / "rollout-b-mine.jsonl", codex_tokens(now - std::chrono::seconds{ 10 }, 300, 20, 200, 10));

    const std::vector<AgentUsageRequest> requests{
        { .instance_id = "codex", .kind = "codex", .working_directory = project,
            .started_at = now - std::chrono::minutes{ 1 } },
    };
    const auto activity = monitor.update(requests, now);
    REQUIRE(activity.contains("codex"));
    CHECK(activity.at("codex").session_tokens == 320);
    CHECK(activity.at("codex").tokens_per_second == Catch::Approx(21.0));
}

TEST_CASE("Server agent projections publish measured activity that clients decay",
    "[agent-usage][server][agent]")
{
    TempDir temp("dx-usage-server");
    const AgentUsageRoots roots{ temp.path / "claude" / "projects", temp.path / "codex" / "sessions" };
    ServerAgentService service("usage",
        std::make_unique<AgentUsageMonitor>(roots, AgentUsageMonitorOptions{ .native_watch = false }));
    const auto now = usage_now();
    const std::string cwd = (temp.path / "work").string();
    const auto file = roots.claude_projects / claude_project_directory_name(cwd) / "live.jsonl";
    append(file, claude_line(now - std::chrono::seconds{ 4 }, "m1", cwd, 100, 0, 100));
    append(file, claude_line(now - std::chrono::seconds{ 2 }, "m2", cwd, 100, 0, 100));

    // A discovered Claude process reports its working directory; the path is
    // used server-side and never published.
    const auto steady_now = std::chrono::steady_clock::now();
    const ServerAgentRuntimeView runtime{
        .space_id = "space-1",
        .tab_id = "tab-1",
        .pane_id = "pane-1",
        .terminal_id = "terminal-1",
        .generation = { 1 },
        .runtime_running = true,
        .process_observation = AgentProcessObservation{
            .captured_at = steady_now,
            .processes = { { .process_id = 7, .parent_process_id = 6,
                .executable = "/usr/local/bin/claude", .working_directory = cwd } },
            .foreground_reliable = true,
        },
    };
    const uint64_t before = service.snapshot().revision;
    service.update({ runtime }, steady_now);
    REQUIRE(service.snapshot().agents.size() == 1);
    const ServerAgentProjection& agent = service.snapshot().agents.front();
    REQUIRE(agent.activity);
    CHECK(agent.activity->session_tokens == 400);
    CHECK(agent.activity->tokens_per_second == Catch::Approx(100.0));
    CHECK(service.snapshot().revision > before);

    const auto json = server_agent_snapshot_to_json(service.snapshot());
    CHECK(json.dump().find(cwd) == std::string::npos);
    std::string error;
    const auto decoded = server_agent_snapshot_from_json(json, error);
    REQUIRE(decoded);
    INFO(error);
    CHECK(decoded->agents.front().activity == agent.activity);
    auto invalid = json;
    invalid["agents"][0]["activity"]["tokens_per_second"] = -1.0;
    CHECK_FALSE(server_agent_snapshot_from_json(invalid, error));

    const auto control = service.handle("agent.get", { { "instance_id", agent.identity.instance_id } });
    REQUIRE(control.ok);
    CHECK(control.value["activity"]["session_tokens"] == 400);

    // Clients roll the published rate off linearly over a minute.
    const AgentActivity activity{ .tokens_per_second = 1000.0, .measured_at_ms = 1'000'000 };
    CHECK(agent_activity_tokens_per_second(activity, 1'000'000) == Catch::Approx(1000.0));
    CHECK(agent_activity_tokens_per_second(activity, 1'030'000) == Catch::Approx(500.0));
    CHECK(agent_activity_tokens_per_second(activity, 1'060'000) == 0.0);
}

TEST_CASE("Hook-reported sessions make hand-started agents in one directory attributable",
    "[agent-usage][server][agent]")
{
    TempDir temp("dx-usage-hooks");
    const AgentUsageRoots roots{ temp.path / "claude" / "projects", temp.path / "codex" / "sessions" };
    ServerAgentService service("hooks",
        std::make_unique<AgentUsageMonitor>(roots, AgentUsageMonitorOptions{ .native_watch = false }));
    const auto now = usage_now();
    const std::string cwd = (temp.path / "repo").string();
    const auto project = roots.claude_projects / claude_project_directory_name(cwd);
    append(project / "first.jsonl", claude_line(now - std::chrono::seconds{ 4 }, "a1", cwd, 100, 0, 0));
    append(project / "first.jsonl", claude_line(now - std::chrono::seconds{ 2 }, "a2", cwd, 100, 0, 0));
    append(project / "second.jsonl", claude_line(now - std::chrono::seconds{ 4 }, "b1", cwd, 10, 0, 0));
    append(project / "second.jsonl", claude_line(now - std::chrono::seconds{ 2 }, "b2", cwd, 10, 0, 0));

    const auto steady_now = std::chrono::steady_clock::now();
    const auto shell_with_claude = [&](std::string terminal, uint64_t pid) {
        return ServerAgentRuntimeView{
            .space_id = "space-1",
            .tab_id = "tab-1",
            .pane_id = "pane-" + terminal,
            .terminal_id = terminal,
            .generation = { 1 },
            .runtime_running = true,
            .process_observation = AgentProcessObservation{
                .captured_at = steady_now,
                .processes = { { .process_id = pid, .parent_process_id = 1,
                    .executable = "/usr/local/bin/claude", .working_directory = cwd } },
                .foreground_reliable = true,
            },
        };
    };
    const auto reference = [](std::string value, uint64_t sequence) {
        return AgentSessionRef{ .source = "draxul:claude", .agent_kind = "claude",
            .integration_version = 3, .sequence = sequence, .kind = AgentSessionRefKind::Id,
            .value = std::move(value) };
    };
    const std::vector<ServerAgentRuntimeView> runtimes{
        shell_with_claude("t1", 11), shell_with_claude("t2", 22)
    };

    // Two hand-started Claude agents share a directory: without native
    // session ids neither can be attributed.
    service.update(runtimes, steady_now);
    REQUIRE(service.snapshot().agents.size() == 2);
    for (const auto& agent : service.snapshot().agents)
        CHECK_FALSE(agent.activity);

    // One hook reports after discovery, the other before its process is
    // discovered (as Claude's SessionStart can); both are applied.
    REQUIRE(service.report_discovered_session("t1", { 1 }, reference("first", 1), steady_now).ok);
    CHECK_FALSE(service.report_discovered_session("t1", { 1 }, reference("first", 1), steady_now).ok);
    ServerAgentService late("late",
        std::make_unique<AgentUsageMonitor>(roots, AgentUsageMonitorOptions{ .native_watch = false }));
    const auto pending = late.report_discovered_session("t2", { 1 }, reference("second", 1), steady_now);
    REQUIRE(pending.ok);
    CHECK(pending.value["pending"] == true);
    // A report for a stale runtime generation never applies.
    REQUIRE(late.report_discovered_session("t1", { 9 }, reference("first", 1), steady_now).ok);

    service.update(runtimes, steady_now);
    late.update(runtimes, steady_now);
    const auto find = [](const ServerAgentService& owner, std::string_view terminal) {
        const auto& agents = owner.snapshot().agents;
        return *std::ranges::find_if(agents, [&](const auto& a) { return a.terminal_id == terminal; });
    };
    const auto first = find(service, "t1");
    REQUIRE(first.session_ref);
    CHECK(first.session_ref->value == "first");
    REQUIRE(first.activity);
    CHECK(first.activity->session_tokens == 200);
    // With its sibling's file claimed, the remaining hand-started agent is no
    // longer ambiguous and takes the other session in the directory.
    const auto unreferenced = find(service, "t2");
    CHECK_FALSE(unreferenced.session_ref);
    REQUIRE(unreferenced.activity);
    CHECK(unreferenced.activity->session_tokens == 20);

    const auto second = find(late, "t2");
    REQUIRE(second.session_ref);
    CHECK(second.session_ref->value == "second");
    REQUIRE(second.activity);
    CHECK(second.activity->session_tokens == 20);
    CHECK_FALSE(find(late, "t1").session_ref);
}

#include <catch2/catch_test_macros.hpp>

#include "control_request_policy.h"

#include <string>
#include <utility>
#include <vector>

using namespace draxul;

namespace
{

struct PolicyFixture
{
    std::vector<std::string> calls;
    std::optional<ControlRequestPolicy::PaneTarget> pane{
        ControlRequestPolicy::PaneTarget{
            .pane_id = "pane-1",
            .space_id = 2,
            .tab_id = 3,
            .has_host = true,
        }
    };
    std::optional<AgentProjection> agent{ AgentProjection{} };
    ControlRequestPolicy::PluginReloadResult reload{
        .generation = "generation-2",
        .matched = 2,
        .reloaded = 2,
    };
    ControlRequestPolicy::NativeRouteResult native_route
        = ControlRequestPolicy::NativeRouteResult::Matched;
    ControlRequestPolicy::NativeSessionResult native_result
        = ControlRequestPolicy::NativeSessionResult::Accepted;
    ControlRequestPolicy::AgentRouteResult agent_route
        = ControlRequestPolicy::AgentRouteResult::Ready;
    ControlRequestPolicy::OperationResult space_focus
        = ControlRequestPolicy::OperationResult::success();
    ControlRequestPolicy::OperationResult agent_focus
        = ControlRequestPolicy::OperationResult::success();
    ControlRequestPolicy::OperationResult agent_restart
        = ControlRequestPolicy::OperationResult::success();
    ControlRequestPolicy::LaunchResult launch{
        .instance_id = "agent-started",
    };
    bool action_accepted = true;
    bool input_accepted = true;
    std::string sent_bytes;
    uint64_t event_cursor = 0;
    size_t event_limit = 0;

    PolicyFixture()
    {
        agent->identity.instance_id = "agent-1";
        agent->generation.value = 7;
        agent->lifecycle = AgentLifecycle::Running;
        agent->status = AgentStatus::Working;
    }

    ControlRequestPolicy make_policy()
    {
        return ControlRequestPolicy({
            .read = [&](const ControlRequest& request) {
                calls.push_back("read:" + request.method);
                return ControlMethodResult::success({
                    { "method", request.method },
                    { "instance_id", request.params.is_object()
                            ? request.params.value("instance_id", "")
                            : "" },
                });
            },
            .find_pane = [&](std::string_view) {
                calls.push_back("find_pane");
                return pane;
            },
            .focus_pane = [&](const ControlRequestPolicy::PaneTarget&) {
                calls.push_back("focus_pane");
                return ControlRequestPolicy::FocusResult::Focused;
            },
            .dispatch_pane_action = [&](const ControlRequestPolicy::PaneTarget&,
                                        std::string_view) {
                calls.push_back("pane_action");
                return action_accepted
                    ? ControlRequestPolicy::ActionResult::Dispatched
                    : ControlRequestPolicy::ActionResult::Rejected;
            },
            .reload_plugin = [&](std::string_view) {
                calls.push_back("reload");
                return reload;
            },
            .inspect_native_route = [&](std::string_view,
                                        std::string_view,
                                        std::string_view) {
                calls.push_back("native_route");
                return native_route;
            },
            .accept_native_session = [&](std::string_view,
                                         std::string_view,
                                         AgentSessionRef) {
                calls.push_back("native_accept");
                return native_result;
            },
            .focus_space = [&](SpaceId) {
                calls.push_back("focus_space");
                return space_focus;
            },
            .launch_agent = [&](AgentLaunchRequest) {
                calls.push_back("launch");
                return launch;
            },
            .find_agent = [&](std::string_view) {
                calls.push_back("find_agent");
                return agent;
            },
            .focus_agent = [&](std::string_view) {
                calls.push_back("focus_agent");
                return agent_focus;
            },
            .restart_agent = [&](const AgentProjection&) {
                calls.push_back("restart_agent");
                return agent_restart;
            },
            .inspect_agent_route = [&](const AgentProjection&) {
                calls.push_back("agent_route");
                return agent_route;
            },
            .send_agent_input = [&](const AgentProjection&, std::string_view bytes) {
                calls.push_back("input");
                sent_bytes = bytes;
                return input_accepted;
            },
            .read_events = [&](uint64_t cursor, size_t limit) {
                calls.push_back("events");
                event_cursor = cursor;
                event_limit = limit;
                return nlohmann::json{
                    { "events", nlohmann::json::array() },
                };
            },
        });
    }
};

ControlRequest native_report(std::string source = "draxul:codex")
{
    return { "native", "pane.report_agent_session",
        {
            { "pane_id", "pane-1" },
            { "agent_instance_id", "agent-1" },
            { "source", std::move(source) },
            { "agent", "codex" },
            { "integration_version", uint32_t{ 1 } },
            { "sequence", uint64_t{ 4 } },
            { "ref_kind", "id" },
            { "ref_value", "session-1" },
        } };
}

} // namespace

TEST_CASE("control request policy preserves effect order around validation",
    "[control][app][policy]")
{
    PolicyFixture fixture;
    auto policy = fixture.make_policy();

    fixture.pane.reset();
    auto missing_pane = policy.handle(
        { "1", "pane.action", { { "pane_id", "missing" } } });
    CHECK_FALSE(missing_pane.ok);
    CHECK(missing_pane.error_code == "not_found");
    CHECK(fixture.calls == std::vector<std::string>{ "find_pane" });

    fixture.calls.clear();
    fixture.pane = ControlRequestPolicy::PaneTarget{
        .pane_id = "pane-1", .space_id = 2, .tab_id = 3, .has_host = true };
    auto missing_action = policy.handle(
        { "2", "pane.action", { { "pane_id", "pane-1" } } });
    CHECK_FALSE(missing_action.ok);
    CHECK(missing_action.error_code == "invalid_params");
    CHECK(fixture.calls == std::vector<std::string>{ "find_pane" });

    fixture.calls.clear();
    auto invalid_args = policy.handle({ "3", "agent.start",
        { { "profile_id", "codex" }, { "space_id", 8 },
            { "args", nlohmann::json::array({ std::string(4097, 'x') }) } } });
    CHECK_FALSE(invalid_args.ok);
    CHECK(invalid_args.error_code == "invalid_params");
    CHECK(fixture.calls == std::vector<std::string>{ "focus_space" });
}

TEST_CASE("control request policy maps pane reload event and fallback results",
    "[control][app][policy]")
{
    PolicyFixture fixture;
    auto policy = fixture.make_policy();

    const auto focused = policy.handle(
        { "1", "pane.focus", { { "pane_id", "pane-1" } } });
    REQUIRE(focused.ok);
    CHECK(focused.value["space_id"] == 2);
    CHECK(focused.value["tab_id"] == 3);

    fixture.reload.error = "reload failed";
    fixture.reload.rolled_back = true;
    const auto reload = policy.handle(
        { "2", "plugin.reload", { { "plugin_id", "example" } } });
    CHECK_FALSE(reload.ok);
    CHECK(reload.error_code == "reload_rolled_back");

    const auto events = policy.handle(
        { "3", "event.subscribe",
            { { "cursor", uint64_t{ 9 } }, { "limit", uint64_t{ 1000 } } } });
    REQUIRE(events.ok);
    CHECK(fixture.event_cursor == 9);
    CHECK(fixture.event_limit == 128);

    const auto fallback = policy.handle({ "4", "space.list", {} });
    REQUIRE(fallback.ok);
    CHECK(fallback.value["method"] == "space.list");
}

TEST_CASE("control request policy validates and accepts native sessions in order",
    "[control][app][policy]")
{
    PolicyFixture fixture;
    auto policy = fixture.make_policy();

    fixture.native_route = ControlRequestPolicy::NativeRouteResult::Mismatch;
    auto mismatch = policy.handle(native_report("unofficial"));
    CHECK_FALSE(mismatch.ok);
    CHECK(mismatch.error_code == "routing_mismatch");
    CHECK(fixture.calls == std::vector<std::string>{ "native_route" });

    fixture.calls.clear();
    fixture.native_route = ControlRequestPolicy::NativeRouteResult::Matched;
    auto invalid = policy.handle(native_report("unofficial"));
    CHECK_FALSE(invalid.ok);
    CHECK(invalid.error_code == "invalid_session_ref");
    CHECK(fixture.calls == std::vector<std::string>{ "native_route" });

    fixture.calls.clear();
    fixture.native_result = ControlRequestPolicy::NativeSessionResult::Duplicate;
    auto duplicate = policy.handle(native_report());
    CHECK_FALSE(duplicate.ok);
    CHECK(duplicate.error_code == "duplicate_session_ref");
    CHECK(fixture.calls
        == std::vector<std::string>{ "native_route", "native_accept" });

    fixture.calls.clear();
    fixture.native_result = ControlRequestPolicy::NativeSessionResult::Accepted;
    auto accepted = policy.handle(native_report());
    REQUIRE(accepted.ok);
    CHECK(accepted.value["instance_id"] == "agent-1");
    CHECK(fixture.calls == std::vector<std::string>{
                               "native_route", "native_accept", "read:agent.get" });
}

TEST_CASE("control request policy owns input encoding and route precedence",
    "[control][app][policy]")
{
    PolicyFixture fixture;
    auto policy = fixture.make_policy();

    fixture.agent_route = ControlRequestPolicy::AgentRouteResult::Missing;
    auto replaced = policy.handle({ "1", "agent.send_text",
        { { "instance_id", "agent-1" }, { "text", 42 } } });
    CHECK_FALSE(replaced.ok);
    CHECK(replaced.error_code == "agent_replaced");
    CHECK(fixture.calls
        == std::vector<std::string>{ "find_agent", "agent_route" });

    fixture.calls.clear();
    fixture.agent_route = ControlRequestPolicy::AgentRouteResult::Ready;
    auto keys = policy.handle({ "2", "agent.send_keys",
        { { "instance_id", "agent-1" },
            { "keys", { "Enter", "CTRL+C", "left" } } } });
    REQUIRE(keys.ok);
    CHECK(fixture.sent_bytes == "\r\x03\x1b[D");
    CHECK(fixture.calls == std::vector<std::string>{
                               "find_agent", "agent_route", "input", "read:agent.get" });

    fixture.calls.clear();
    fixture.input_accepted = false;
    auto rejected = policy.handle({ "3", "agent.send_text",
        { { "instance_id", "agent-1" }, { "text", "hello" } } });
    CHECK_FALSE(rejected.ok);
    CHECK(rejected.error_code == "input_failed");
}

TEST_CASE("control request policy owns wait generation and predicate precedence",
    "[control][app][policy]")
{
    PolicyFixture fixture;
    fixture.agent->lifecycle = AgentLifecycle::Exited;
    fixture.agent->status = AgentStatus::Done;
    auto policy = fixture.make_policy();

    fixture.agent_route = ControlRequestPolicy::AgentRouteResult::NoLiveHost;
    auto defaults = policy.handle(
        { "1", "agent.wait", { { "instance_id", "agent-1" } } });
    REQUIRE(defaults.ok);
    CHECK(defaults.value["complete"] == true);
    CHECK(defaults.value["outcome"] == "done");

    fixture.calls.clear();
    auto lifecycle = policy.handle({ "2", "agent.wait",
        { { "instance_id", "agent-1" }, { "until", { "exited" } } } });
    REQUIRE(lifecycle.ok);
    CHECK(lifecycle.value["outcome"] == "exited");

    fixture.calls.clear();
    auto generation = policy.handle({ "3", "agent.wait",
        { { "instance_id", "agent-1" },
            { "runtime_generation", uint64_t{ 6 } },
            { "until", 17 } } });
    REQUIRE(generation.ok);
    CHECK(generation.value["outcome"] == "agent_replaced");
    CHECK(fixture.calls == std::vector<std::string>{
                               "find_agent", "agent_route", "read:agent.get" });
}

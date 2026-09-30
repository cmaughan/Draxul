#include <catch2/catch_all.hpp>

#include "agent_controller.h"
#include <draxul/integration_cli.h>
#include "app.h"
#include <draxul/control_cli.h>
#include "control_event_journal.h"
#include "control_request_router.h"
#include "space_controller.h"
#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <draxul/control_plane.h>
#include <draxul/server_client.h>

#include <chrono>
#include <future>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string test_font_path()
{
    return draxul::tests::bundled_font_path().string();
}

ControlClientResult request_while_pumping(App& app,
    std::string_view session_id,
    const std::filesystem::path& runtime,
    std::string_view method,
    nlohmann::json params)
{
    auto future = std::async(std::launch::async,
        [=] { return ControlClient::request(session_id, runtime, method, params); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (future.wait_for(std::chrono::milliseconds(0))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        app.run_smoke_test(std::chrono::milliseconds(5));
    }
    if (future.wait_for(std::chrono::milliseconds(0))
        != std::future_status::ready)
        return { false, nullptr, "timeout", "Test control request timed out." };
    return future.get();
}

} // namespace

TEST_CASE("control event subscriptions are bounded cursor projections", "[control]")
{
    ControlEventJournal journal;
    journal.record("space.focused", { { "space_id", 2 } });
    journal.record("agent.changed", { { "instance_id", "agent-1" } });

    const auto first = journal.read_after(0, 1);
    REQUIRE(first["events"].size() == 1);
    CHECK(first["events"][0]["type"] == "space.focused");
    const uint64_t cursor = first["next_cursor"].get<uint64_t>();

    const auto second = journal.read_after(cursor, 4);
    REQUIRE(second["events"].size() == 1);
    CHECK(second["events"][0]["type"] == "agent.changed");
    CHECK_FALSE(second["cursor_expired"].get<bool>());
}

TEST_CASE("control router projects Spaces without exposing mutable state", "[control]")
{
    SpaceController spaces("D:/work/project");
    AgentController agents;
    ControlRequestRouter router(spaces, agents, "test-session");

    auto hello = router.handle({ "1", "system.hello", {} });
    REQUIRE(hello.ok);
    CHECK(hello.value["session_id"] == "test-session");
    CHECK(hello.value["protocol_version"] == kControlProtocolVersion);

    auto list = router.handle({ "2", "space.list", {} });
    REQUIRE(list.ok);
    REQUIRE(list.value.size() == 1);
    CHECK(list.value[0]["id"] == kDefaultSpaceId);
    CHECK(list.value[0]["active"] == true);

    auto missing = router.handle({ "3", "space.get", { { "id", 99 } } });
    CHECK_FALSE(missing.ok);
    CHECK(missing.error_code == "not_found");
}

TEST_CASE("app control endpoint starts, prompts, waits, and emits events", "[control][app]")
{
    TempDir home("dxl-ctl");
    HomeDirRedirect redirect(home.path);
    std::vector<FakeHost*> hosts;

    AppOptions options;
    options.load_user_config = false;
    options.save_user_config = false;
    options.activate_window_on_startup = false;
    options.clamp_window_to_display = false;
    options.override_display_ppi = 96.0f;
    options.config_overrides.font_path = test_font_path();
    options.window_factory = [] { return std::make_unique<FakeWindow>(); };
    options.renderer_create_fn = [](int, RendererOptions) {
        return RendererBundle{ std::make_unique<FakeTermRenderer>() };
    };
    options.host_factory = [&](HostKind) {
        auto host = std::make_unique<FakeHost>("control-host");
        hosts.push_back(host.get());
        return host;
    };
    options.session_id = "ctl-app";
    options.control_id = "ctl-ui-17";
    options.executable_path = "D:/build/draxul.exe";
    options.server_runtime_directory = "D:/runtime/server";
    options.enable_control_server = true;

    App app(std::move(options));
    REQUIRE(app.initialize());
    const auto runtime = control_runtime_directory(
        ConfigDocument::default_path().parent_path());

    auto started = request_while_pumping(app, "ctl-ui-17", runtime,
        "agent.start", { { "profile_id", "codex" }, { "args", nlohmann::json::array() } });
    REQUIRE(started.ok);
    const std::string instance_id = started.result.value("instance_id", "");
    const std::string pane_id = started.result["route"].value("pane_id", "");
    REQUIRE_FALSE(instance_id.empty());
    REQUIRE(hosts.size() == 2);

    const auto has_environment = [](const HostLaunchOptions& launch,
                                     std::string_view name,
                                     std::string_view value) {
        return std::ranges::any_of(launch.environment,
            [=](const auto& entry) {
                return entry.first == name && entry.second == value;
            });
    };
    CHECK(has_environment(hosts.front()->captured_launch,
        "DRAXUL_SESSION_ID", "ctl-app"));
    CHECK(has_environment(hosts.front()->captured_launch,
        "DRAXUL_CONTROL_ID", "ctl-ui-17"));
    CHECK(has_environment(hosts.front()->captured_launch,
        "DRAXUL_EXECUTABLE", "D:/build/draxul.exe"));
    CHECK(has_environment(hosts.front()->captured_launch,
        "DRAXUL_SERVER_RUNTIME_DIR", "D:/runtime/server"));

    auto action = request_while_pumping(app, "ctl-ui-17", runtime,
        "pane.action", { { "pane_id", pane_id },
                           { "action", "rezonality_reload" } });
    REQUIRE(action.ok);
    REQUIRE(hosts.back()->dispatched_actions.size() == 1);
    CHECK(hosts.back()->dispatched_actions.back() == "rezonality_reload");

    auto focused = request_while_pumping(app, "ctl-ui-17", runtime,
        "pane.focus", { { "pane_id", pane_id } });
    REQUIRE(focused.ok);
    CHECK(focused.result["pane_id"] == pane_id);
    CHECK(focused.result["active"] == true);

    auto reported = request_while_pumping(app, "ctl-ui-17", runtime,
        "pane.report_agent_session",
        { { "pane_id", pane_id }, { "agent_instance_id", instance_id },
            { "source", "draxul:codex" }, { "agent", "codex" },
            { "integration_version", 1 }, { "sequence", 1 },
            { "ref_kind", "id" }, { "ref_value", "codex-session-1" } });
    REQUIRE(reported.ok);
    CHECK(reported.result["native_session"]["source"] == "draxul:codex");

    auto sent = request_while_pumping(app, "ctl-ui-17", runtime,
        "agent.send_text",
        { { "instance_id", instance_id }, { "text", "Review this" } });
    REQUIRE(sent.ok);
    REQUIRE(hosts.back()->sent_agent_input.size() == 1);
    CHECK(hosts.back()->sent_agent_input.back() == "Review this");

    auto waiting = request_while_pumping(app, "ctl-ui-17", runtime,
        "agent.wait",
        { { "instance_id", instance_id }, { "until", { "done" } } });
    REQUIRE(waiting.ok);
    CHECK_FALSE(waiting.result.value("complete", true));
    CHECK(waiting.result["agent"]["runtime_generation"].get<uint64_t>() > 0);

    auto events = request_while_pumping(
        app, "ctl-ui-17", runtime, "event.subscribe",
        nlohmann::json::object());
    INFO(events.error_code << ": " << events.error_message);
    REQUIRE(events.ok);
    CHECK_FALSE(events.result["events"].empty());

    app.shutdown();
    CHECK_FALSE(std::filesystem::exists(
        control_metadata_path(runtime, "ctl-ui-17")));
}

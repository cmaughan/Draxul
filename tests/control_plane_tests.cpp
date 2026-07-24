#include <catch2/catch_all.hpp>

#include "agent_controller.h"
#include "control_cli.h"
#include "control_request_router.h"
#include "space_controller.h"

#include <draxul/control_plane.h>

#include <chrono>
#include <future>
#include <thread>

using namespace draxul;

namespace
{

std::filesystem::path unique_runtime_directory()
{
    return std::filesystem::temp_directory_path()
        / ("draxul-control-test-"
            + std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()));
}

} // namespace

TEST_CASE("control CLI recognizes read-only Space and pane commands", "[control][cli]")
{
    auto spaces = parse_control_cli(
        { "draxul", "space", "list", "--session", "work", "--json" });
    REQUIRE(spaces.command);
    CHECK(spaces.command->method == "space.list");
    CHECK(spaces.command->session_id == "work");
    CHECK(spaces.command->json);

    auto pane =
        parse_control_cli({ "draxul", "pane", "read", "pane-4", "--lines", "25" });
    REQUIRE(pane.command);
    CHECK(pane.command->method == "pane.read");
    CHECK(pane.command->value == "pane-4");
    CHECK(pane.command->lines == 25);

    auto invalid =
        parse_control_cli({ "draxul", "pane", "read", "pane-4", "--lines", "201" });
    CHECK(invalid.recognized);
    CHECK(invalid.error);
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

TEST_CASE("control transport authenticates and dispatches on the caller thread", "[control]")
{
    const auto runtime = unique_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start("transport-test", runtime, [] {}, &start_error));
    REQUIRE(std::filesystem::exists(server.metadata_path()));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request(
            "transport-test", runtime, "system.hello", { { "probe", true } });
    });

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");
    CHECK(response.result["probe"] == true);

    const auto metadata = server.metadata_path();
    server.stop();
    CHECK_FALSE(std::filesystem::exists(metadata));
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

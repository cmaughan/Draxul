#include <catch2/catch_all.hpp>

#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/app_options.h>

#include "space_controller.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct SpaceHostHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;
    std::vector<FakeHost*> hosts;
    std::vector<std::shared_ptr<int>> shutdown_counts;

    PaneManager::Deps make_deps()
    {
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto shutdown_count = std::make_shared<int>(0);
            auto host = std::make_unique<FakeHost>("space-host");
            host->on_shutdown_callback = [shutdown_count] { ++*shutdown_count; };
            shutdown_counts.push_back(shutdown_count);
            hosts.push_back(host.get());
            return host;
        };

        PaneManager::Deps deps;
        deps.options = &options;
        deps.config = &config;
        deps.window = &window;
        deps.grid_renderer = &renderer;
        deps.display_ppi = &display_ppi;
        deps.compute_viewport = [](const PaneDescriptor&) {
            return HostViewport{ .pixel_size = { 800, 600 }, .grid_size = { 100, 37 } };
        };
        return deps;
    }

    bool create_initial_tab(Space& space)
    {
        return space.tab_controller.create_initial_tab(callbacks, 800, 600, make_deps());
    }
};

} // namespace

TEST_CASE("space controller creates one active default space", "[space_controller][space]")
{
    const std::filesystem::path root = "D:/work/project";
    SpaceController controller(root);

    REQUIRE(controller.count() == 1);
    REQUIRE(controller.spaces().size() == 1);
    CHECK(controller.active_space_id() == kDefaultSpaceId);
    REQUIRE(controller.find_active_space() != nullptr);
    CHECK(controller.find_active_space()->id == kDefaultSpaceId);
    CHECK(controller.find_active_space()->name == "default");
    CHECK(controller.find_active_space()->root_directory == root);
    CHECK(&controller.active_tab_controller()
        == &controller.find_active_space()->tab_controller);
}

TEST_CASE("space controller shutdown retains the default space and clears its tabs",
    "[space_controller][space][shutdown]")
{
    SpaceController controller;
    auto& tabs = controller.active_tab_controller();
    tabs.tabs().push_back(std::make_unique<Tab>(7, PaneManager::Deps{}));
    REQUIRE(tabs.activate_tab(7));

    controller.shutdown_all();

    REQUIRE(controller.count() == 1);
    CHECK(controller.active_space_id() == kDefaultSpaceId);
    REQUIRE(controller.find_active_space() != nullptr);
    CHECK(controller.active_tab_controller().empty());
    CHECK(controller.active_tab_controller().active_tab_id() == -1);
}

TEST_CASE("space controller creates and updates inactive space metadata",
    "[space_controller][space]")
{
    SpaceController controller;

    const SpaceId first = controller.create_space("compiler", "D:/work/compiler");
    const SpaceId second = controller.create_space("renderer");

    REQUIRE(first == 1);
    REQUIRE(second == 2);
    REQUIRE(controller.count() == 3);
    REQUIRE(controller.find_space(first) != nullptr);
    CHECK(controller.find_space(first)->name == "compiler");
    CHECK(controller.find_space(first)->root_directory == "D:/work/compiler");
    CHECK_FALSE(controller.find_space(first)->tab_controller.focus_enabled());
    CHECK(controller.active_space_id() == kDefaultSpaceId);

    REQUIRE(controller.rename_space(first, "compiler-agent"));
    REQUIRE(controller.set_space_root_directory(first, "D:/work/compiler-next"));
    CHECK(controller.find_space(first)->name == "compiler-agent");
    CHECK(controller.find_space(first)->root_directory == "D:/work/compiler-next");

    CHECK(controller.create_space("") == kInvalidSpaceId);
    CHECK_FALSE(controller.rename_space(first, ""));
    CHECK_FALSE(controller.rename_space(99, "missing"));
    CHECK_FALSE(controller.set_space_root_directory(99, "D:/missing"));
    CHECK(controller.find_space(99) == nullptr);
}

TEST_CASE("space controller refuses to activate an empty space",
    "[space_controller][space][focus]")
{
    SpaceController controller;
    const SpaceId empty = controller.create_space("empty");

    CHECK_FALSE(controller.activate_space(empty));
    CHECK(controller.active_space_id() == kDefaultSpaceId);
    CHECK(controller.activate_space(kDefaultSpaceId));
}

TEST_CASE("space switching transfers focus and preserves inactive hosts",
    "[space_controller][space][focus][lifecycle]")
{
    SpaceHostHarness harness;
    SpaceController controller;
    REQUIRE(harness.create_initial_tab(*controller.find_space(kDefaultSpaceId)));
    FakeHost* default_host = harness.hosts[0];
    CHECK(default_host->focus_gained_calls == 1);

    const SpaceId worker = controller.create_space("worker", "D:/work/worker");
    REQUIRE(harness.create_initial_tab(*controller.find_space(worker)));
    FakeHost* worker_host = harness.hosts[1];
    CHECK(worker_host->focus_gained_calls == 0);

    REQUIRE(controller.activate_space(worker));
    CHECK(controller.active_space_id() == worker);
    CHECK(default_host->focus_lost_calls == 1);
    CHECK(worker_host->focus_gained_calls == 1);
    CHECK(*harness.shutdown_counts[0] == 0);
    CHECK(*harness.shutdown_counts[1] == 0);

    REQUIRE(controller.activate_space(kDefaultSpaceId));
    CHECK(worker_host->focus_lost_calls == 1);
    CHECK(default_host->focus_gained_calls == 2);
    CHECK(*harness.shutdown_counts[0] == 0);
    CHECK(*harness.shutdown_counts[1] == 0);

    REQUIRE(controller.close_space(worker));
    CHECK(controller.count() == 1);
    CHECK(controller.find_space(worker) == nullptr);
    CHECK(*harness.shutdown_counts[1] == 1);
    CHECK_FALSE(controller.close_space(kDefaultSpaceId));
}

TEST_CASE("closing the active space focuses a populated replacement before shutdown",
    "[space_controller][space][focus][lifecycle]")
{
    SpaceHostHarness harness;
    SpaceController controller;
    REQUIRE(harness.create_initial_tab(*controller.find_space(kDefaultSpaceId)));
    FakeHost* default_host = harness.hosts[0];

    const SpaceId worker = controller.create_space("worker");
    REQUIRE(harness.create_initial_tab(*controller.find_space(worker)));
    FakeHost* worker_host = harness.hosts[1];
    REQUIRE(controller.activate_space(worker));
    CHECK(worker_host->focus_gained_calls == 1);

    REQUIRE(controller.close_space(worker));
    CHECK(controller.active_space_id() == kDefaultSpaceId);
    CHECK(controller.count() == 1);
    CHECK(default_host->focus_gained_calls == 2);
    CHECK(*harness.shutdown_counts[1] == 1);
    CHECK(*harness.shutdown_counts[0] == 0);
}

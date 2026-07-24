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
    bool fail_next_initialize = false;

    PaneManager::Deps make_deps()
    {
        options.host_kind = HostKind::PowerShell;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto shutdown_count = std::make_shared<int>(0);
            auto host = std::make_unique<FakeHost>("space-host");
            host->fail_initialize = fail_next_initialize;
            if (fail_next_initialize)
                host->init_error_message = "injected restore failure";
            fail_next_initialize = false;
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

TabSnapshot make_tab_snapshot(int id, std::string name)
{
    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);
    TabSnapshot tab;
    tab.id = id;
    tab.name = std::move(name);
    tab.name_user_set = true;
    tab.pane_layout.tree = tree.snapshot();
    tab.pane_layout.panes.push_back({
        .leaf_id = leaf,
        .launch = { .kind = HostKind::PowerShell, .command = "pwsh" },
        .pane_id = "pane-" + std::to_string(id),
    });
    return tab;
}

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

TEST_CASE("space controller snapshots every Space in display order",
    "[space_controller][space][session]")
{
    SpaceHostHarness harness;
    SpaceController controller("D:/work/default");
    Space* default_space = controller.find_space(kDefaultSpaceId);
    REQUIRE(default_space != nullptr);
    REQUIRE(harness.create_initial_tab(*default_space));
    default_space->tab_controller.tabs().front()->name = "default-tab";

    const SpaceId worker_id = controller.create_space("worker", "D:/work/worker");
    Space* worker = controller.find_space(worker_id);
    REQUIRE(worker != nullptr);
    REQUIRE(harness.create_initial_tab(*worker));
    worker->tab_controller.tabs().front()->name = "worker-tab";
    REQUIRE(controller.activate_space(worker_id));

    REQUIRE(controller.all_spaces_restorable());
    auto snapshots = controller.snapshot_spaces();
    REQUIRE(snapshots);
    REQUIRE(snapshots->size() == 2);
    CHECK((*snapshots)[0].id == kDefaultSpaceId);
    CHECK((*snapshots)[0].root_directory == std::filesystem::path("D:/work/default"));
    REQUIRE((*snapshots)[0].tabs.size() == 1);
    CHECK((*snapshots)[0].tabs[0].name == "default-tab");
    CHECK((*snapshots)[1].id == worker_id);
    CHECK((*snapshots)[1].name == "worker");
    CHECK((*snapshots)[1].root_directory == std::filesystem::path("D:/work/worker"));
    REQUIRE((*snapshots)[1].tabs.size() == 1);
    CHECK((*snapshots)[1].tabs[0].name == "worker-tab");

    controller.shutdown_all();
}

TEST_CASE("space controller transactionally restores every Space and stable counter",
    "[space_controller][space][session][restore]")
{
    SpaceHostHarness harness;
    SpaceController controller;
    REQUIRE(harness.create_initial_tab(*controller.find_space(kDefaultSpaceId)));
    const auto live_shutdown = harness.shutdown_counts.front();

    SpaceSnapshot default_space;
    default_space.id = 4;
    default_space.name = "compiler";
    default_space.root_directory = "D:/work/compiler";
    default_space.active_tab_id = 7;
    default_space.next_tab_id = 12;
    default_space.tabs.push_back(make_tab_snapshot(7, "build"));

    SpaceSnapshot worker_space;
    worker_space.id = 9;
    worker_space.name = "worker";
    worker_space.root_directory = "D:/work/worker";
    worker_space.active_tab_id = 3;
    worker_space.next_tab_id = 5;
    worker_space.tabs.push_back(make_tab_snapshot(3, "agent"));

    std::vector<SpaceSnapshot> snapshots;
    snapshots.push_back(std::move(default_space));
    snapshots.push_back(std::move(worker_space));

    REQUIRE(controller.restore_spaces(harness.callbacks, 800, 600,
        snapshots, 9, 15,
        [&harness](const Space*) { return harness.make_deps(); }));

    CHECK(*live_shutdown == 1);
    REQUIRE(controller.count() == 2);
    CHECK(controller.spaces()[0]->id == 4);
    CHECK(controller.spaces()[1]->id == 9);
    CHECK(controller.active_space_id() == 9);
    CHECK(controller.next_space_id() == 15);
    CHECK(controller.find_space(4)->root_directory == std::filesystem::path("D:/work/compiler"));
    CHECK(controller.find_space(4)->tab_controller.active_tab_id() == 7);
    CHECK_FALSE(controller.find_space(4)->tab_controller.focus_enabled());
    CHECK(controller.find_space(9)->tab_controller.active_tab_id() == 3);
    CHECK(controller.find_space(9)->tab_controller.focus_enabled());
    CHECK(controller.last_restore_warning().empty());

    controller.shutdown_all();
}

TEST_CASE("space controller preserves live Spaces when no restore candidate is usable",
    "[space_controller][space][session][restore]")
{
    SpaceHostHarness harness;
    SpaceController controller;
    REQUIRE(harness.create_initial_tab(*controller.find_space(kDefaultSpaceId)));
    FakeHost* live_host = harness.hosts.front();
    const auto live_shutdown = harness.shutdown_counts.front();

    SpaceSnapshot broken;
    broken.id = 8;
    broken.name = "broken";
    broken.active_tab_id = 2;
    broken.next_tab_id = 3;
    broken.tabs.push_back(make_tab_snapshot(2, "broken-tab"));

    std::vector<SpaceSnapshot> broken_snapshots;
    broken_snapshots.push_back(std::move(broken));
    harness.fail_next_initialize = true;
    CHECK_FALSE(controller.restore_spaces(harness.callbacks, 800, 600,
        broken_snapshots, 8, 9,
        [&harness](const Space*) { return harness.make_deps(); }));

    CHECK(controller.count() == 1);
    CHECK(controller.active_space_id() == kDefaultSpaceId);
    CHECK(controller.active_tab_controller().active_tab_id() == 0);
    CHECK(live_host->is_running());
    CHECK(*live_shutdown == 0);
    CHECK_FALSE(controller.last_restore_error().empty());

    controller.shutdown_all();
}

TEST_CASE("space controller recovery skips a failed tab and keeps usable topology",
    "[space_controller][space][session][restore][recovery]")
{
    SpaceHostHarness harness;
    SpaceController controller;

    SpaceSnapshot recovered;
    recovered.id = 6;
    recovered.name = "recovered";
    recovered.active_tab_id = 2;
    recovered.next_tab_id = 10;
    recovered.tabs.push_back(make_tab_snapshot(2, "failed"));
    recovered.tabs.push_back(make_tab_snapshot(7, "usable"));

    std::vector<SpaceSnapshot> recovered_snapshots;
    recovered_snapshots.push_back(std::move(recovered));
    harness.fail_next_initialize = true;
    REQUIRE(controller.restore_spaces(harness.callbacks, 800, 600,
        recovered_snapshots, 6, 8,
        [&harness](const Space*) { return harness.make_deps(); }));

    REQUIRE(controller.count() == 1);
    const Space* space = controller.find_space(6);
    REQUIRE(space != nullptr);
    REQUIRE(space->tab_controller.count() == 1);
    CHECK(space->tab_controller.tabs().front()->id == 7);
    CHECK(space->tab_controller.active_tab_id() == 7);
    CHECK(space->tab_controller.next_tab_id() == 10);
    CHECK(controller.last_restore_warning().find("tab 2 was skipped")
        != std::string::npos);

    controller.shutdown_all();
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

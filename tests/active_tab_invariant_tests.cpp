#include <catch2/catch_all.hpp>

#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/app_options.h>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace draxul
{

struct AppTestAccess
{
    static void add_empty_tab(App& app, int id)
    {
        app.space_controller_.active_tab_controller().tabs().push_back(
            std::make_unique<Tab>(id, PaneManager::Deps{}));
    }

    static void clear_tabs_without_resetting_active_id(App& app)
    {
        app.space_controller_.active_tab_controller().tabs().clear();
    }

    static Tab* find_active(App& app)
    {
        return app.find_active_tab();
    }

    static Tab& require_active(App& app)
    {
        return app.require_active_tab("active tab invariant test");
    }

    static bool close(App& app, int id)
    {
        return app.close_tab(id);
    }

    static void activate(App& app, int id)
    {
        app.activate_tab(id);
    }

    static size_t count(const App& app)
    {
        return app.space_controller_.active_tab_controller().tabs().size();
    }

    static int active_id(const App& app)
    {
        return app.space_controller_.active_tab_controller().active_tab_id();
    }

    static bool restore(App& app, const SessionSnapshot& state)
    {
        return app.restore_session_state(800, 600, state);
    }

    static void shutdown_without_persistence(App& app)
    {
        app.discard_session_state_on_shutdown_ = true;
        app.shutdown();
    }

    static SpaceId create_space(App& app, std::string name)
    {
        return app.space_controller_.create_space(std::move(name));
    }

    static Space* find_space(App& app, SpaceId id)
    {
        return app.space_controller_.find_space(id);
    }

    static void pump_background_hosts(App& app)
    {
        app.pump_background_hosts();
    }

    static int wait_timeout_ms(App& app)
    {
        return app.wait_timeout_ms(std::nullopt);
    }

    static void request_quit(App& app)
    {
        app.request_quit();
    }
};

} // namespace draxul

namespace
{

struct InactiveSpaceHostHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    std::vector<FakeHost*> hosts;

    PaneManager::Deps make_deps()
    {
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto created = std::make_unique<FakeHost>("inactive-space-host");
            hosts.push_back(created.get());
            return created;
        };

        PaneManager::Deps deps;
        deps.options = &options;
        deps.config = &config;
        deps.window = &window;
        deps.grid_renderer = &renderer;
        deps.compute_viewport = [](const PaneDescriptor&) {
            return HostViewport{ .pixel_size = { 800, 600 }, .grid_size = { 100, 37 } };
        };
        return deps;
    }
};

} // namespace

TEST_CASE("active tab lookup makes empty and stale states explicit", "[app][tab]")
{
    App app;
    CHECK(AppTestAccess::find_active(app) == nullptr);
    CHECK_THROWS_AS(AppTestAccess::require_active(app), std::logic_error);

    AppTestAccess::add_empty_tab(app, 7);
    AppTestAccess::activate(app, 7);
    AppTestAccess::clear_tabs_without_resetting_active_id(app);
    CHECK(AppTestAccess::find_active(app) == nullptr);
    CHECK_THROWS_AS(AppTestAccess::require_active(app), std::logic_error);

    AppTestAccess::activate(app, 99);
    CHECK(AppTestAccess::active_id(app) == 7);
    CHECK(AppTestAccess::find_active(app) == nullptr);

    AppTestAccess::add_empty_tab(app, 7);
    AppTestAccess::activate(app, 7);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK(AppTestAccess::find_active(app)->id == 7);
}

TEST_CASE("closing an active tab selects its replacement before destruction", "[app][tab]")
{
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::add_empty_tab(app, 2);
    AppTestAccess::activate(app, 1);

    REQUIRE(AppTestAccess::close(app, 1));
    CHECK(AppTestAccess::count(app) == 1);
    CHECK(AppTestAccess::active_id(app) == 2);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK(AppTestAccess::find_active(app)->id == 2);
}

TEST_CASE("closing an inactive tab preserves active identity and the last tab", "[app][tab]")
{
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::add_empty_tab(app, 2);
    AppTestAccess::activate(app, 1);

    REQUIRE(AppTestAccess::close(app, 2));
    CHECK(AppTestAccess::active_id(app) == 1);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK_FALSE(AppTestAccess::close(app, 1));
    CHECK(AppTestAccess::count(app) == 1);
    CHECK(AppTestAccess::active_id(app) == 1);
}

TEST_CASE("tab controller cycles, activates by index, and reorders the active tab",
    "[tab_controller][tab]")
{
    TabController controller;
    controller.tabs().push_back(std::make_unique<Tab>(1, PaneManager::Deps{}));
    controller.tabs().push_back(std::make_unique<Tab>(2, PaneManager::Deps{}));
    controller.tabs().push_back(std::make_unique<Tab>(3, PaneManager::Deps{}));

    REQUIRE(controller.activate_tab(2));
    controller.next_tab();
    CHECK(controller.active_tab_id() == 3);
    controller.prev_tab();
    CHECK(controller.active_tab_id() == 2);

    controller.move_tab(-1);
    REQUIRE(controller.tabs().size() == 3);
    CHECK(controller.tabs()[0]->id == 2);
    CHECK(controller.tabs()[1]->id == 1);
    CHECK(controller.tabs()[2]->id == 3);

    controller.activate_tab_by_index(3);
    CHECK(controller.active_tab_id() == 3);
    controller.activate_tab_by_index(0);
    CHECK(controller.active_tab_id() == 3);
}

TEST_CASE("failed restore and empty shutdown leave an explicit no-tab state", "[app][tab][session]")
{
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::activate(app, 1);

    SessionSnapshot invalid;
    invalid.tabs.push_back(TabSnapshot{ .id = 9 });
    CHECK_FALSE(AppTestAccess::restore(app, invalid));
    CHECK(AppTestAccess::count(app) == 0);
    CHECK(AppTestAccess::active_id(app) == -1);
    CHECK(AppTestAccess::find_active(app) == nullptr);

    CHECK_NOTHROW(AppTestAccess::shutdown_without_persistence(app));
    CHECK(AppTestAccess::active_id(app) == -1);
}

TEST_CASE("app services and closes hosts in background tabs and spaces",
    "[app][space][lifecycle]")
{
    InactiveSpaceHostHarness harness;
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::activate(app, 1);

    const SpaceId inactive_id = AppTestAccess::create_space(app, "inactive");
    Space* inactive = AppTestAccess::find_space(app, inactive_id);
    REQUIRE(inactive != nullptr);
    REQUIRE(inactive->tab_controller.create_initial_tab(
        harness.callbacks, 800, 600, harness.make_deps()));
    REQUIRE(harness.hosts.size() == 1);
    FakeHost* inactive_space_host = harness.hosts[0];

    Space* active = AppTestAccess::find_space(app, kDefaultSpaceId);
    REQUIRE(active != nullptr);
    REQUIRE(active->tab_controller.add_tab(
                harness.callbacks, 800, 600, harness.make_deps(), HostKind::Nvim)
        >= 0);
    REQUIRE(active->tab_controller.activate_tab(1));
    REQUIRE(harness.hosts.size() == 2);
    FakeHost* inactive_tab_host = harness.hosts[1];

    AppTestAccess::pump_background_hosts(app);
    CHECK(inactive_space_host->pump_calls == 1);
    CHECK(inactive_tab_host->pump_calls == 1);
    CHECK(AppTestAccess::wait_timeout_ms(app) == 50);

    AppTestAccess::request_quit(app);
    CHECK(inactive_space_host->request_close_calls == 1);
    CHECK(inactive_tab_host->request_close_calls == 1);
}

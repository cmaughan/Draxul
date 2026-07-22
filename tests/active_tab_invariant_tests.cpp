#include <catch2/catch_all.hpp>

#include "app.h"

using namespace draxul;

namespace draxul
{

struct AppTestAccess
{
    static void add_empty_tab(App& app, int id)
    {
        app.tabs_.push_back(std::make_unique<Tab>(id, HostManager::Deps{}));
    }

    static void set_active_id(App& app, int id)
    {
        app.active_tab_id_ = id;
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
        return app.tabs_.size();
    }

    static int active_id(const App& app)
    {
        return app.active_tab_id_;
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
};

} // namespace draxul

TEST_CASE("active tab lookup makes empty and stale states explicit", "[app][tab]")
{
    App app;
    CHECK(AppTestAccess::find_active(app) == nullptr);
    CHECK_THROWS_AS(AppTestAccess::require_active(app), std::logic_error);

    AppTestAccess::add_empty_tab(app, 7);
    AppTestAccess::set_active_id(app, 99);
    CHECK(AppTestAccess::find_active(app) == nullptr);
    CHECK_THROWS_AS(AppTestAccess::require_active(app), std::logic_error);

    AppTestAccess::activate(app, 99);
    CHECK(AppTestAccess::active_id(app) == 99);
    CHECK(AppTestAccess::find_active(app) == nullptr);

    AppTestAccess::activate(app, 7);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK(AppTestAccess::find_active(app)->id == 7);
}

TEST_CASE("closing an active tab selects its replacement before destruction", "[app][tab]")
{
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::add_empty_tab(app, 2);
    AppTestAccess::set_active_id(app, 1);

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
    AppTestAccess::set_active_id(app, 1);

    REQUIRE(AppTestAccess::close(app, 2));
    CHECK(AppTestAccess::active_id(app) == 1);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK_FALSE(AppTestAccess::close(app, 1));
    CHECK(AppTestAccess::count(app) == 1);
    CHECK(AppTestAccess::active_id(app) == 1);
}

TEST_CASE("failed restore and empty shutdown leave an explicit no-tab state", "[app][tab][session]")
{
    App app;
    AppTestAccess::add_empty_tab(app, 1);
    AppTestAccess::set_active_id(app, 1);

    SessionSnapshot invalid;
    invalid.tabs.push_back(TabSnapshot{ .id = 9 });
    CHECK_FALSE(AppTestAccess::restore(app, invalid));
    CHECK(AppTestAccess::count(app) == 0);
    CHECK(AppTestAccess::active_id(app) == -1);
    CHECK(AppTestAccess::find_active(app) == nullptr);

    CHECK_NOTHROW(AppTestAccess::shutdown_without_persistence(app));
    CHECK(AppTestAccess::active_id(app) == -1);
}

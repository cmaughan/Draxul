#include <catch2/catch_all.hpp>

#include "app.h"

using namespace draxul;

namespace draxul
{

struct AppTestAccess
{
    static void add_empty_workspace(App& app, int id)
    {
        app.workspaces_.push_back(std::make_unique<Workspace>(id, HostManager::Deps{}));
    }

    static void set_active_id(App& app, int id)
    {
        app.active_workspace_ = id;
    }

    static Workspace* find_active(App& app)
    {
        return app.find_active_workspace();
    }

    static Workspace& require_active(App& app)
    {
        return app.require_active_workspace("active workspace invariant test");
    }

    static bool close(App& app, int id)
    {
        return app.close_workspace(id);
    }

    static void activate(App& app, int id)
    {
        app.activate_workspace(id);
    }

    static size_t count(const App& app)
    {
        return app.workspaces_.size();
    }

    static int active_id(const App& app)
    {
        return app.active_workspace_;
    }

    static bool restore(App& app, const AppSessionState& state)
    {
        return app.restore_session_state(800, 600, state);
    }

    static void shutdown_without_persistence(App& app)
    {
        app.session_killed_ = true;
        app.shutdown();
    }
};

} // namespace draxul

TEST_CASE("active workspace lookup makes empty and stale states explicit", "[app][workspace]")
{
    App app;
    CHECK(AppTestAccess::find_active(app) == nullptr);
    CHECK_THROWS_AS(AppTestAccess::require_active(app), std::logic_error);

    AppTestAccess::add_empty_workspace(app, 7);
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

TEST_CASE("closing an active workspace selects its replacement before destruction", "[app][workspace]")
{
    App app;
    AppTestAccess::add_empty_workspace(app, 1);
    AppTestAccess::add_empty_workspace(app, 2);
    AppTestAccess::set_active_id(app, 1);

    REQUIRE(AppTestAccess::close(app, 1));
    CHECK(AppTestAccess::count(app) == 1);
    CHECK(AppTestAccess::active_id(app) == 2);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK(AppTestAccess::find_active(app)->id == 2);
}

TEST_CASE("closing an inactive workspace preserves active identity and the last workspace", "[app][workspace]")
{
    App app;
    AppTestAccess::add_empty_workspace(app, 1);
    AppTestAccess::add_empty_workspace(app, 2);
    AppTestAccess::set_active_id(app, 1);

    REQUIRE(AppTestAccess::close(app, 2));
    CHECK(AppTestAccess::active_id(app) == 1);
    REQUIRE(AppTestAccess::find_active(app) != nullptr);
    CHECK_FALSE(AppTestAccess::close(app, 1));
    CHECK(AppTestAccess::count(app) == 1);
    CHECK(AppTestAccess::active_id(app) == 1);
}

TEST_CASE("failed restore and empty shutdown leave an explicit no-workspace state", "[app][workspace][session]")
{
    App app;
    AppTestAccess::add_empty_workspace(app, 1);
    AppTestAccess::set_active_id(app, 1);

    AppSessionState invalid;
    invalid.workspaces.push_back(WorkspaceSessionState{ .id = 9 });
    CHECK_FALSE(AppTestAccess::restore(app, invalid));
    CHECK(AppTestAccess::count(app) == 0);
    CHECK(AppTestAccess::active_id(app) == -1);
    CHECK(AppTestAccess::find_active(app) == nullptr);

    CHECK_NOTHROW(AppTestAccess::shutdown_without_persistence(app));
    CHECK(AppTestAccess::active_id(app) == -1);
}

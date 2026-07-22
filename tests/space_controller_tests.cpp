#include <catch2/catch_all.hpp>

#include "space_controller.h"

using namespace draxul;

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

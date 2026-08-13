#include "support/megacity_scene_test_support.h"

#ifdef DRAXUL_ENABLE_MEGACITY

#include "city_selection.h"
#include "scene_publication.h"
#include "semantic_source_controller.h"

TEST_CASE("megacity selection identity includes source module and qualified name", "[megacity][selection]")
{
    const std::string first = exact_building_identity_key("src/a.cpp", "app", "Widget");
    const std::string second = exact_building_identity_key("src/b.cpp", "app", "Widget");
    const std::string third = exact_building_identity_key("src/a.cpp", "tools", "Widget");

    CHECK(first != second);
    CHECK(first != third);
    CHECK(first == exact_building_identity_key("src/a.cpp", "app", "Widget"));
}

TEST_CASE("megacity selection resolves dependencies through function bundles", "[megacity][selection]")
{
    SemanticMegacityModel model;
    model.function_bundle_remap.emplace("app::run", "Functions");
    model.dependencies.push_back({
        "app", "app::run", "", "Widget", "app", "Widget",
        "src/run.cpp", "include/widget.h", false,
    });

    const auto connected = connected_building_identities(model, "", "app", "Functions", "app::run");
    CHECK(connected.contains(exact_building_identity_key("include/widget.h", "app", "Widget")));
}

TEST_CASE("megacity scene publication preserves a visible tooltip", "[megacity][publication]")
{
    CodeVizSceneWorld world;
    IsometricCamera camera;
    camera.set_viewport(320, 200);
    camera.frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
    MegaCityCodeConfig config;
    CodeVizScenePass pass(1, 1, world.tile_size());
    pass.scene().tooltip = {
        .visible = true,
        .screen_pos = { 20.0f, 30.0f },
        .width = 1,
        .height = 1,
        .rgba = { 255, 255, 255, 255 },
        .revision = 7,
    };

    const float world_span = publish_scene_snapshot(
        pass, camera, world, config, nullptr, nullptr, nullptr, nullptr, true);

    CHECK(world_span > 0.0f);
    CHECK(pass.scene().tooltip.valid());
    CHECK(pass.scene().tooltip.revision == 7);
}

TEST_CASE("megacity semantic source lifecycle is inert before start", "[megacity][lifecycle]")
{
    SemanticSourceController source("unused");
    CHECK_FALSE(source.started());
    CHECK_FALSE(source.ready());
    CHECK_FALSE(source.poll().has_value());

    source.stop();
    CHECK_FALSE(source.started());
    CHECK(source.root() == std::filesystem::path("unused"));
}

#endif

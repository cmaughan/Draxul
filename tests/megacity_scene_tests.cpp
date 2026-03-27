#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "mesh_library.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
#include <SDL3/SDL.h>
#include <draxul/megacity_host.h>
#include <draxul/text_service.h>
#include <numbers>
#include <thread>

using namespace draxul;

namespace
{

float triangle_up_normal_y(const MeshData& mesh, size_t triangle_index)
{
    const size_t base = triangle_index * 3;
    const glm::vec3 p0 = mesh.vertices[mesh.indices[base + 0]].position;
    const glm::vec3 p1 = mesh.vertices[mesh.indices[base + 1]].position;
    const glm::vec3 p2 = mesh.vertices[mesh.indices[base + 2]].position;
    return glm::cross(p1 - p0, p2 - p0).y;
}

glm::vec2 ndc_of_point(const SceneSnapshot& scene, const glm::vec3& point)
{
    const glm::vec4 clip = scene.camera.proj * scene.camera.view * glm::vec4(point, 1.0f);
    return glm::vec2(clip) / clip.w;
}

SceneSnapshot snapshot_from_camera(const IsometricCamera& camera)
{
    SceneSnapshot scene;
    scene.camera.view = camera.view_matrix();
    scene.camera.proj = camera.proj_matrix();
    return scene;
}

void pump_until_idle(MegaCityHost& host, int max_steps = 64)
{
    for (int i = 0; i < max_steps; ++i)
    {
        const auto next_tick = host.next_deadline();
        if (!next_tick.has_value())
            return;
        std::this_thread::sleep_until(*next_tick);
        host.pump();
    }
}

struct TestLotRect
{
    float min_x;
    float max_x;
    float min_z;
    float max_z;
};

TestLotRect test_building_lot(const SemanticCityBuilding& building)
{
    const MegaCityCodeConfig config;
    const float step = std::max(config.placement_step, 0.01f);
    const float raw_half_extent = building.metrics.footprint * 0.5f + building.metrics.sidewalk_width
        + building.metrics.road_width;
    const float half_extent = std::max(step, std::round(raw_half_extent / step) * step);
    return {
        building.center.x - half_extent,
        building.center.x + half_extent,
        building.center.y - half_extent,
        building.center.y + half_extent,
    };
}

bool test_lots_overlap(const TestLotRect& a, const TestLotRect& b)
{
    return a.min_x < b.max_x && a.max_x > b.min_x && a.min_z < b.max_z && a.max_z > b.min_z;
}

} // namespace

TEST_CASE("megacity world maps grid coordinates to tile centers", "[megacity]")
{
    SceneWorld world;

    const glm::vec3 origin = world.grid_to_world(0, 0);
    const glm::vec3 corner = world.grid_to_world(4, 4);
    const glm::vec3 elevated = world.grid_to_world(2, 3, 2.0f);
    const glm::vec3 fractional = world.grid_to_world(2.25f, 3.5f);

    CHECK(origin.x == Catch::Approx(0.5f));
    CHECK(origin.y == Catch::Approx(0.0f));
    CHECK(origin.z == Catch::Approx(0.5f));

    CHECK(corner.x == Catch::Approx(4.5f));
    CHECK(corner.z == Catch::Approx(4.5f));

    CHECK(elevated.x == Catch::Approx(2.5f));
    CHECK(elevated.y == Catch::Approx(2.0f));
    CHECK(elevated.z == Catch::Approx(3.5f));

    CHECK(fractional.x == Catch::Approx(2.75f));
    CHECK(fractional.z == Catch::Approx(4.0f));
}

TEST_CASE("megacity world starts empty", "[megacity]")
{
    SceneWorld world;

    const auto view = world.registry().view<const Appearance>();
    CHECK(view.begin() == view.end());
}

TEST_CASE("megacity camera projection responds to viewport aspect", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);

    camera.set_viewport(100, 100);
    const glm::mat4 square = camera.proj_matrix();

    camera.set_viewport(200, 100);
    const glm::mat4 wide = camera.proj_matrix();

    CHECK(wide[0][0] < square[0][0]);
    CHECK(wide[1][1] == Catch::Approx(square[1][1]));
}

TEST_CASE("megacity camera footprint covers the centered world", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);

    const GroundFootprint footprint = camera.visible_ground_footprint();

    CHECK(footprint.min_x < 0.5f);
    CHECK(footprint.max_x > 4.5f);
    CHECK(footprint.min_z < 0.5f);
    CHECK(footprint.max_z > 4.5f);
}

TEST_CASE("megacity camera footprint follows a retargeted focus point", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);

    const GroundFootprint centered = camera.visible_ground_footprint();
    camera.set_target({ 14.5f, 0.0f, 9.5f });
    const GroundFootprint shifted = camera.visible_ground_footprint();

    CHECK(shifted.min_x > centered.min_x + 8.0f);
    CHECK(shifted.max_x > centered.max_x + 8.0f);
    CHECK(shifted.min_z > centered.min_z + 4.0f);
    CHECK(shifted.max_z > centered.max_z + 4.0f);
}

TEST_CASE("megacity camera grows the far clip for large framed worlds", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-120.0f, 120.0f, -90.0f, 90.0f);

    CHECK(camera.far_plane() > 300.0f);
}

TEST_CASE("megacity camera zoom is clamped between close and city-scale framing", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-30.0f, 30.0f, -20.0f, 20.0f);

    camera.zoom_by(-20.0f);
    CHECK(camera.zoom_half_height() == Catch::Approx(2.5f));

    camera.zoom_by(20.0f);
    CHECK(camera.zoom_half_height() == Catch::Approx(60.0f));
}

TEST_CASE("megacity camera pitch is clamped to a sensible range", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-10.0f, 10.0f, -10.0f, 10.0f);

    camera.adjust_pitch(10.0f);
    CHECK(camera.pitch_angle() == Catch::Approx(1.22173048f));

    camera.adjust_pitch(-10.0f);
    CHECK(camera.pitch_angle() == Catch::Approx(0.43633231f));
}

TEST_CASE("megacity camera state round-trips through apply_state", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-30.0f, 30.0f, -20.0f, 20.0f);

    const IsometricCameraState desired{
        .target = { 12.5f, 0.0f, -7.25f },
        .yaw = -1.2f,
        .pitch = 0.9f,
        .orbit_radius = 42.0f,
        .zoom_half_height = 17.5f,
    };
    camera.apply_state(desired);

    const IsometricCameraState actual = camera.state();
    CHECK(actual.target.x == Catch::Approx(desired.target.x));
    CHECK(actual.target.y == Catch::Approx(desired.target.y));
    CHECK(actual.target.z == Catch::Approx(desired.target.z));
    CHECK(actual.yaw == Catch::Approx(desired.yaw));
    CHECK(actual.pitch == Catch::Approx(desired.pitch));
    CHECK(actual.orbit_radius == Catch::Approx(desired.orbit_radius));
    CHECK(actual.zoom_half_height == Catch::Approx(desired.zoom_half_height));
}

TEST_CASE("semantic city layout starts with the tallest building at the origin", "[megacity]")
{
    std::vector<CityClassRecord> rows;

    CityClassRecord app;
    app.qualified_name = "App";
    app.source_file_path = "app/app.h";
    app.entity_kind = "building";
    app.base_size = 16;
    app.building_functions = 9;
    app.function_sizes = { 24, 18, 14 };
    app.road_size = 4;
    rows.push_back(app);

    CityClassRecord dispatcher;
    dispatcher.qualified_name = "InputDispatcher";
    dispatcher.source_file_path = "app/input_dispatcher.h";
    dispatcher.entity_kind = "building";
    dispatcher.base_size = 6;
    dispatcher.building_functions = 4;
    dispatcher.function_sizes = { 12, 10 };
    dispatcher.road_size = 2;
    rows.push_back(dispatcher);

    CityClassRecord gui;
    gui.qualified_name = "GuiActionHandler";
    gui.source_file_path = "app/gui_action_handler.h";
    gui.entity_kind = "building";
    gui.base_size = 4;
    gui.building_functions = 3;
    gui.function_sizes = { 9, 8 };
    gui.road_size = 1;
    rows.push_back(gui);

    CityClassRecord abstract_type;
    abstract_type.qualified_name = "IHost";
    abstract_type.source_file_path = "libs/draxul-host/include/draxul/host.h";
    abstract_type.entity_kind = "tower";
    abstract_type.is_abstract = true;
    abstract_type.base_size = 2;
    abstract_type.building_functions = 5;
    abstract_type.function_sizes = { 6, 6, 6 };
    abstract_type.road_size = 3;
    rows.push_back(abstract_type);

    const MegaCityCodeConfig config;
    const SemanticCityLayout layout = build_semantic_city_layout(rows, config);

    REQUIRE(layout.buildings.size() == 3);
    CHECK(layout.buildings[0].qualified_name == "App");
    // First building is no longer at (0,0) — the park occupies the center.
    CHECK(layout.park_footprint > 0.0f);
    CHECK(layout.min_x < 0.0f);
    CHECK(layout.max_x > 0.0f);
    CHECK(layout.min_z < 0.0f);
    CHECK(layout.max_z > 0.0f);

    REQUIRE(layout.buildings[0].layers.size() == 3);
    const float total_layer_height = layout.buildings[0].layers[0].height
        + layout.buildings[0].layers[1].height
        + layout.buildings[0].layers[2].height;
    CHECK(total_layer_height == Catch::Approx(layout.buildings[0].metrics.height));
    CHECK(layout.buildings[0].layers[0].function_size == 24);
    CHECK(layout.buildings[0].layers[1].function_size == 18);
    CHECK(layout.buildings[0].layers[2].function_size == 14);
    CHECK(layout.buildings[0].layers[0].height > layout.buildings[0].layers[1].height);
    CHECK(layout.buildings[0].layers[1].height > layout.buildings[0].layers[2].height);

    for (size_t i = 0; i < layout.buildings.size(); ++i)
    {
        for (size_t j = i + 1; j < layout.buildings.size(); ++j)
        {
            CHECK_FALSE(test_lots_overlap(
                test_building_lot(layout.buildings[i]),
                test_building_lot(layout.buildings[j])));
        }
    }
}

TEST_CASE("semantic building metrics can bypass clamping", "[megacity]")
{
    CityClassRecord row;
    row.entity_kind = "building";
    row.base_size = 144;
    row.building_functions = 36;
    row.function_sizes = { 200, 180, 160, 140, 120, 100, 80 };
    row.road_size = 80;

    MegaCityCodeConfig clamped_config;
    clamped_config.clamp_semantic_metrics = true;
    MegaCityCodeConfig unclamped_config;
    unclamped_config.clamp_semantic_metrics = false;
    const BuildingMetrics clamped = derive_building_metrics(row, clamped_config);
    const BuildingMetrics unclamped = derive_building_metrics(row, unclamped_config);

    CHECK(clamped.footprint == Catch::Approx(9.0f));
    CHECK(clamped.height == Catch::Approx(12.0f));
    CHECK(clamped.sidewalk_width == Catch::Approx(1.0f));
    CHECK(clamped.road_width == Catch::Approx(3.0f));

    CHECK(unclamped.footprint > clamped.footprint);
    CHECK(unclamped.height > clamped.height);
    CHECK(unclamped.sidewalk_width == Catch::Approx(clamped.sidewalk_width));
    CHECK(unclamped.road_width > clamped.road_width);
}

TEST_CASE("semantic city lot reserve matches the full visible road width by default", "[megacity]")
{
    CityClassRecord row;
    row.name = "MegaCityHost";
    row.qualified_name = "MegaCityHost";
    row.module_path = "libs/draxul-megacity";
    row.source_file_path = "libs/draxul-megacity/include/draxul/megacity_host.h";
    row.entity_kind = "building";
    row.base_size = 55;
    row.building_functions = 33;
    row.function_sizes = { 24, 18, 14, 10 };
    row.road_size = 24;

    const MegaCityCodeConfig config;
    const BuildingMetrics metrics = derive_building_metrics(row, config);
    const SemanticCityBuilding building{
        row.module_path,
        row.name,
        row.qualified_name,
        row.source_file_path,
        row.base_size,
        row.building_functions,
        0,
        row.road_size,
        metrics,
        { 0.0f, 0.0f },
        {},
    };

    const TestLotRect lot = test_building_lot(building);
    const float step = std::max(config.placement_step, 0.01f);
    const float required_half_extent = metrics.footprint * 0.5f + metrics.sidewalk_width + metrics.road_width;
    CHECK(lot.max_x >= required_half_extent);
    CHECK(-lot.min_x >= required_half_extent);
    CHECK(lot.max_x <= required_half_extent + step * 0.5f + 1e-4f);
    CHECK(-lot.min_x <= required_half_extent + step * 0.5f + 1e-4f);
}

TEST_CASE("road width scale affects unclamped semantic road width", "[megacity]")
{
    CityClassRecord row;
    row.entity_kind = "building";
    row.base_size = 12;
    row.building_functions = 6;
    row.function_sizes = { 18, 12, 8 };
    row.road_size = 24;

    MegaCityCodeConfig low_scale_config;
    low_scale_config.clamp_semantic_metrics = false;
    low_scale_config.road_width_scale = 0.25f;

    MegaCityCodeConfig high_scale_config = low_scale_config;
    high_scale_config.road_width_scale = 1.25f;

    const BuildingMetrics low_scale = derive_building_metrics(row, low_scale_config);
    const BuildingMetrics high_scale = derive_building_metrics(row, high_scale_config);

    CHECK(high_scale.road_width > low_scale.road_width);
}

TEST_CASE("semantic city layout handles very large unclamped lots", "[megacity]")
{
    CityClassRecord alpha;
    alpha.name = "Alpha";
    alpha.qualified_name = "Alpha";
    alpha.module_path = "libs/draxul-megacity";
    alpha.source_file_path = "libs/draxul-megacity/src/alpha.cpp";
    alpha.entity_kind = "building";
    alpha.base_size = 6400;
    alpha.building_functions = 24;
    alpha.function_sizes = { 200, 180, 160, 140, 120 };
    alpha.road_size = 48;

    CityClassRecord beta = alpha;
    beta.name = "Beta";
    beta.qualified_name = "Beta";
    beta.source_file_path = "libs/draxul-megacity/src/beta.cpp";

    MegaCityCodeConfig config;
    config.clamp_semantic_metrics = false;

    const SemanticCityLayout layout = build_semantic_city_layout({ alpha, beta }, config);

    REQUIRE(layout.buildings.size() == 2);
    CHECK_FALSE(test_lots_overlap(
        test_building_lot(layout.buildings[0]),
        test_building_lot(layout.buildings[1])));
}

TEST_CASE("semantic city layout can hide test entities by source path", "[megacity]")
{
    CityClassRecord app_row;
    app_row.name = "App";
    app_row.qualified_name = "App";
    app_row.module_path = "app";
    app_row.source_file_path = "app/app.cpp";
    app_row.entity_kind = "building";
    app_row.base_size = 8;
    app_row.building_functions = 4;
    app_row.function_sizes = { 20, 16 };
    app_row.road_size = 2;

    CityClassRecord test_row;
    test_row.name = "FakeRenderer";
    test_row.qualified_name = "FakeRenderer";
    test_row.module_path = "tests";
    test_row.source_file_path = "tests/support/fake_renderer.h";
    test_row.entity_kind = "building";
    test_row.base_size = 4;
    test_row.building_functions = 3;
    test_row.function_sizes = { 10, 8 };
    test_row.road_size = 1;

    const std::vector<CityClassRecord> rows{ app_row, test_row };
    MegaCityCodeConfig visible_config;
    visible_config.clamp_semantic_metrics = true;
    visible_config.hide_test_entities = false;
    MegaCityCodeConfig hidden_config = visible_config;
    hidden_config.hide_test_entities = true;
    const SemanticCityLayout visible = build_semantic_city_layout(rows, visible_config);
    const SemanticCityLayout hidden = build_semantic_city_layout(rows, hidden_config);

    REQUIRE(visible.buildings.size() == 2);
    REQUIRE(hidden.buildings.size() == 1);
    CHECK(hidden.buildings[0].qualified_name == "App");
    CHECK(is_test_semantic_source(test_row.source_file_path));
    CHECK_FALSE(is_test_semantic_source(app_row.source_file_path));
}

TEST_CASE("semantic megacity model is built from DB rows and shared metrics", "[megacity]")
{
    SemanticCityModuleInput app;
    app.module_path = "app";

    CityClassRecord main_window;
    main_window.name = "App";
    main_window.qualified_name = "App";
    main_window.module_path = app.module_path;
    main_window.source_file_path = "app/app.cpp";
    main_window.entity_kind = "building";
    main_window.base_size = 24;
    main_window.building_functions = 5;
    main_window.function_sizes = { 20, 16, 12 };
    main_window.road_size = 4;
    app.rows.push_back(main_window);

    CityClassRecord dispatcher = main_window;
    dispatcher.name = "InputDispatcher";
    dispatcher.qualified_name = "InputDispatcher";
    dispatcher.source_file_path = "app/input_dispatcher.cpp";
    dispatcher.base_size = 8;
    dispatcher.building_functions = 3;
    dispatcher.function_sizes = { 10, 8 };
    dispatcher.road_size = 2;
    app.rows.push_back(dispatcher);

    const MegaCityCodeConfig config;
    const SemanticMegacityModel model = build_semantic_megacity_model({ app }, config);

    REQUIRE(model.modules.size() == 1);
    REQUIRE(model.building_count() == 2);
    CHECK(model.modules[0].module_path == "app");
    CHECK(model.modules[0].connectivity == 6);
    CHECK(model.modules[0].buildings[0].qualified_name == "App");
    CHECK(model.modules[0].buildings[0].base_size == 24);
    CHECK(model.modules[0].buildings[0].function_count == 5);
    CHECK(model.modules[0].buildings[0].function_mass == 48);
    CHECK(model.modules[0].buildings[0].road_size == 4);

    const SemanticMegacityLayout layout = build_semantic_megacity_layout(model, config);
    REQUIRE(layout.modules.size() == 1); // single module, no central park
    CHECK_FALSE(layout.modules[0].is_central_park);
    REQUIRE(layout.building_count() == 2);
    CHECK(layout.modules[0].buildings[0].qualified_name == "App");
    // Park occupies the center; first building is offset from origin.
    CHECK(layout.modules[0].park_footprint > 0.0f);
    CHECK(layout.modules[0].buildings[0].metrics.height
        == Catch::Approx(model.modules[0].buildings[0].metrics.height));
}

TEST_CASE("semantic city road strips form a square ring around a building", "[megacity]")
{
    SemanticCityBuilding building;
    building.center = { 0.0f, 0.0f };
    building.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 1.0f,
    };

    const auto roads = build_road_segments(building);

    CHECK(roads[0].center.x == Catch::Approx(0.0f));
    CHECK(roads[0].center.y == Catch::Approx(3.5f));
    CHECK(roads[0].extent.x == Catch::Approx(8.0f));
    CHECK(roads[0].extent.y == Catch::Approx(1.0f));

    CHECK(roads[1].center.x == Catch::Approx(0.0f));
    CHECK(roads[1].center.y == Catch::Approx(-3.5f));
    CHECK(roads[1].extent.x == Catch::Approx(8.0f));
    CHECK(roads[1].extent.y == Catch::Approx(1.0f));

    CHECK(roads[2].center.x == Catch::Approx(-3.5f));
    CHECK(roads[2].center.y == Catch::Approx(0.0f));
    CHECK(roads[2].extent.x == Catch::Approx(1.0f));
    CHECK(roads[2].extent.y == Catch::Approx(6.0f));

    CHECK(roads[3].center.x == Catch::Approx(3.5f));
    CHECK(roads[3].center.y == Catch::Approx(0.0f));
    CHECK(roads[3].extent.x == Catch::Approx(1.0f));
    CHECK(roads[3].extent.y == Catch::Approx(6.0f));
}

TEST_CASE("semantic city sidewalks form a ring between a building and its roads", "[megacity]")
{
    SemanticCityBuilding building;
    building.center = { 0.0f, 0.0f };
    building.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 1.0f,
    };

    const auto sidewalks = build_sidewalk_segments(building);

    CHECK(sidewalks[0].center == glm::vec2(0.0f, 2.5f));
    CHECK(sidewalks[0].extent == glm::vec2(6.0f, 1.0f));
    CHECK(sidewalks[1].center == glm::vec2(0.0f, -2.5f));
    CHECK(sidewalks[1].extent == glm::vec2(6.0f, 1.0f));
    CHECK(sidewalks[2].center == glm::vec2(-2.5f, 0.0f));
    CHECK(sidewalks[2].extent == glm::vec2(1.0f, 4.0f));
    CHECK(sidewalks[3].center == glm::vec2(2.5f, 0.0f));
    CHECK(sidewalks[3].extent == glm::vec2(1.0f, 4.0f));
}

TEST_CASE("semantic megacity road surface spans the shared building footprint envelope", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module_layout;

    SemanticCityBuilding building_a;
    building_a.center = { 0.0f, 0.0f };
    building_a.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 3.0f,
    };

    SemanticCityBuilding building_b;
    building_b.center = { 8.0f, 0.0f };
    building_b.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 3.0f,
    };

    module_layout.buildings = { building_a, building_b };
    layout.modules.push_back(std::move(module_layout));

    const CitySurfaceBounds bounds = compute_city_road_surface_bounds(layout);

    REQUIRE(bounds.valid());
    CHECK(bounds.min_x == Catch::Approx(-6.0f));
    CHECK(bounds.max_x == Catch::Approx(14.0f));
    CHECK(bounds.min_z == Catch::Approx(-6.0f));
    CHECK(bounds.max_z == Catch::Approx(6.0f));
}

TEST_CASE("city grid uses one shared road surface under the building envelope", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module_layout;

    SemanticCityBuilding building_a;
    building_a.center = { 0.0f, 0.0f };
    building_a.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 3.0f,
    };

    SemanticCityBuilding building_b;
    building_b.center = { 8.0f, 0.0f };
    building_b.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 3.0f,
    };

    module_layout.buildings = { building_a, building_b };
    layout.modules.push_back(std::move(module_layout));
    layout.min_x = -5.0f;
    layout.max_x = 13.0f;
    layout.min_z = -5.0f;
    layout.max_z = 5.0f;

    MegaCityCodeConfig config;
    config.placement_step = 0.5f;
    const CityGrid grid = build_city_grid(layout, config);

    auto sample_cell = [&](float world_x, float world_z) {
        const int col = static_cast<int>(std::floor((world_x - grid.origin_x) / grid.cell_size));
        const int row = static_cast<int>(std::floor((world_z - grid.origin_z) / grid.cell_size));
        return grid.at(col, row);
    };

    CHECK(sample_cell(0.0f, 0.0f) == kCityGridBuilding);
    CHECK(sample_cell(2.5f, 0.0f) == kCityGridSidewalk);
    CHECK(sample_cell(4.0f, 0.0f) == kCityGridRoad);
    CHECK(sample_cell(-4.0f, 0.0f) == kCityGridRoad);
}

TEST_CASE("roof sign mesh textures only the top face", "[megacity]")
{
    const MeshData mesh = build_roof_sign_mesh();

    REQUIRE(mesh.vertices.size() == 24);
    REQUIRE(mesh.indices.size() == 36);

    size_t textured_vertices = 0;
    size_t top_facing_textured_vertices = 0;
    for (const auto& vertex : mesh.vertices)
    {
        if (vertex.tex_blend > 0.5f)
        {
            textured_vertices++;
            if (vertex.normal.y > 0.5f)
                top_facing_textured_vertices++;
        }
    }

    CHECK(textured_vertices == 4);
    CHECK(top_facing_textured_vertices == 4);
}

TEST_CASE("wall sign mesh textures only the front face", "[megacity]")
{
    const MeshData mesh = build_wall_sign_mesh();

    REQUIRE(mesh.vertices.size() == 24);
    REQUIRE(mesh.indices.size() == 36);

    size_t textured_vertices = 0;
    size_t front_facing_textured_vertices = 0;
    for (const auto& vertex : mesh.vertices)
    {
        if (vertex.tex_blend > 0.5f)
        {
            textured_vertices++;
            if (vertex.normal.z > 0.5f)
                front_facing_textured_vertices++;
        }
    }

    CHECK(textured_vertices == 4);
    CHECK(front_facing_textured_vertices == 4);
}

TEST_CASE("semantic city layout places later lots in edge contact with existing roads", "[megacity]")
{
    std::vector<CityClassRecord> rows;

    CityClassRecord app;
    app.qualified_name = "App";
    app.source_file_path = "app/app.h";
    app.entity_kind = "building";
    app.base_size = 16;
    app.building_functions = 9;
    app.function_sizes = { 24, 18, 14 };
    app.road_size = 4;
    rows.push_back(app);

    CityClassRecord dispatcher = app;
    dispatcher.qualified_name = "InputDispatcher";
    dispatcher.source_file_path = "app/input_dispatcher.h";
    rows.push_back(dispatcher);

    const MegaCityCodeConfig config;
    const SemanticCityLayout layout = build_semantic_city_layout(rows, config);

    REQUIRE(layout.buildings.size() == 2);
    const TestLotRect a = test_building_lot(layout.buildings[0]);
    const TestLotRect b = test_building_lot(layout.buildings[1]);

    // Buildings must not overlap each other.
    CHECK_FALSE(test_lots_overlap(a, b));

    // With the park occupying the center, the second building may share an edge
    // with the park rather than with building A directly. Check that buildings
    // are in edge contact with each other OR that each touches the park.
    const float park_half = layout.park_footprint * 0.5f;
    const TestLotRect park_lot{
        layout.park_center.x - park_half,
        layout.park_center.x + park_half,
        layout.park_center.y - park_half,
        layout.park_center.y + park_half,
    };

    auto shares_edge_with = [](const TestLotRect& p, const TestLotRect& q) {
        const bool touch_x = p.max_x == Catch::Approx(q.min_x) || q.max_x == Catch::Approx(p.min_x);
        const bool touch_z = p.max_z == Catch::Approx(q.min_z) || q.max_z == Catch::Approx(p.min_z);
        const float overlap_x = std::min(p.max_x, q.max_x) - std::max(p.min_x, q.min_x);
        const float overlap_z = std::min(p.max_z, q.max_z) - std::max(p.min_z, q.min_z);
        return (touch_x && overlap_z > 0.0f) || (touch_z && overlap_x > 0.0f);
    };

    const bool a_b_touch = shares_edge_with(a, b);
    const bool a_park_touch = shares_edge_with(a, park_lot);
    const bool b_park_touch = shares_edge_with(b, park_lot);
    CHECK((a_b_touch || (a_park_touch && b_park_touch)));
}

TEST_CASE("semantic megacity layout spirals modules around the largest module", "[megacity]")
{
    SemanticCityModuleInput app;
    app.module_path = "app";

    CityClassRecord app_main;
    app_main.module_path = app.module_path;
    app_main.qualified_name = "App";
    app_main.source_file_path = "app/app.h";
    app_main.entity_kind = "building";
    app_main.base_size = 16;
    app_main.building_functions = 9;
    app_main.function_sizes = { 24, 18, 14 };
    app_main.road_size = 4;
    app.rows.push_back(app_main);

    CityClassRecord dispatcher = app_main;
    dispatcher.qualified_name = "InputDispatcher";
    dispatcher.source_file_path = "app/input_dispatcher.h";
    dispatcher.base_size = 8;
    dispatcher.building_functions = 5;
    dispatcher.function_sizes = { 12, 10, 8 };
    dispatcher.road_size = 2;
    app.rows.push_back(dispatcher);

    SemanticCityModuleInput host;
    host.module_path = "libs/draxul-host";

    CityClassRecord terminal;
    terminal.module_path = host.module_path;
    terminal.qualified_name = "TerminalHostBase";
    terminal.source_file_path = "libs/draxul-host/include/draxul/terminal_host_base.h";
    terminal.entity_kind = "building";
    terminal.base_size = 6;
    terminal.building_functions = 4;
    terminal.function_sizes = { 10, 8 };
    terminal.road_size = 2;
    host.rows.push_back(terminal);

    const MegaCityCodeConfig config;
    const SemanticMegacityLayout layout = build_semantic_megacity_layout({ host, app }, config);

    REQUIRE(layout.modules.size() == 3); // central_park + 2 real modules
    CHECK(layout.modules[0].is_central_park);
    REQUIRE(layout.building_count() == 3);
    CHECK(layout.modules[1].module_path == "app");

    const auto& centered = layout.modules[1];
    const auto& neighbor = layout.modules[2];
    const bool overlaps = centered.min_x < neighbor.max_x && centered.max_x > neighbor.min_x
        && centered.min_z < neighbor.max_z && centered.max_z > neighbor.min_z;
    const bool moved_off_origin = std::abs(neighbor.offset.x) > 0.0f || std::abs(neighbor.offset.y) > 0.0f;

    CHECK(moved_off_origin);
    CHECK_FALSE(overlaps);
    CHECK(neighbor.buildings[0].module_path == neighbor.module_path);
}

TEST_CASE("megacity camera orbit keeps looking at the same world focus", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);
    camera.set_target({ 8.5f, 0.0f, 6.5f });

    const glm::mat4 before_view = camera.view_matrix();
    const GroundFootprint before = camera.visible_ground_footprint();
    camera.orbit_target(std::numbers::pi_v<float> * 0.5f);
    const glm::mat4 after_view = camera.view_matrix();
    const GroundFootprint after = camera.visible_ground_footprint();

    CHECK(after.min_x < 8.5f);
    CHECK(after.max_x > 8.5f);
    CHECK(after.min_z < 6.5f);
    CHECK(after.max_z > 6.5f);
    CHECK(after_view[2][0] != Catch::Approx(before_view[2][0]));
    CHECK(after_view[0][2] != Catch::Approx(before_view[0][2]));
    CHECK(before.min_x < 8.5f);
    CHECK(before.max_x > 8.5f);
}

TEST_CASE("megacity camera planar axes follow the current view", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);

    const glm::vec2 initial_right = camera.planar_right_vector();
    const glm::vec2 initial_up = camera.planar_up_vector();
    camera.orbit_target(std::numbers::pi_v<float> * 0.5f);
    const glm::vec2 rotated_right = camera.planar_right_vector();
    const glm::vec2 rotated_up = camera.planar_up_vector();

    CHECK(glm::length(initial_right) == Catch::Approx(1.0f));
    CHECK(glm::length(initial_up) == Catch::Approx(1.0f));
    CHECK(std::abs(glm::dot(initial_right, initial_up)) < 0.01f);
    CHECK(glm::length(rotated_right) == Catch::Approx(1.0f));
    CHECK(glm::length(rotated_up) == Catch::Approx(1.0f));
    CHECK(std::abs(glm::dot(rotated_right, rotated_up)) < 0.01f);
    CHECK(std::abs(glm::dot(initial_right, rotated_right)) < 0.01f);
    CHECK(std::abs(glm::dot(initial_up, rotated_up)) < 0.01f);
}

TEST_CASE("megacity camera screen drag moves content in the same direction", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(240, 120);

    const glm::vec3 probe{ 0.5f, 0.0f, 0.5f };
    const glm::vec2 before = ndc_of_point(snapshot_from_camera(camera), probe);

    const glm::vec2 pan = camera.pan_delta_for_screen_drag(glm::vec2(32.0f, -18.0f));
    camera.translate_target(pan.x, pan.y);

    const glm::vec2 after = ndc_of_point(snapshot_from_camera(camera), probe);

    CHECK(after.x > before.x);
    CHECK(after.y > before.y);
}

TEST_CASE("megacity host mouse drag pans and alt-drag rotates", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::MegaCity;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context(&window, &renderer, &text_service, std::move(launch), viewport, window.display_ppi_);

    REQUIRE(host.initialize(context, callbacks));
    host.attach_3d_renderer(renderer);

    auto* pass = dynamic_cast<IsometricScenePass*>(renderer.last_render_pass.get());
    REQUIRE(pass != nullptr);

    const glm::vec3 probe{ 0.5f, 0.0f, 0.5f };
    const glm::vec2 before_pan = ndc_of_point(pass->scene(), probe);

    host.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 400, 300 } });
    host.on_mouse_move({ kModNone, { 452, 252 } });
    host.pump();

    const glm::vec2 after_pan = ndc_of_point(pass->scene(), probe);
    CHECK(after_pan.x > before_pan.x);
    CHECK(after_pan.y > before_pan.y);

    const glm::mat4 before_rotate = pass->scene().camera.view;
    host.on_mouse_move({ kModAlt, { 520, 252 } });
    host.pump();
    pump_until_idle(host);

    const glm::mat4 after_rotate = pass->scene().camera.view;
    CHECK(after_rotate[0][0] != Catch::Approx(before_rotate[0][0]));
    CHECK(after_rotate[2][0] != Catch::Approx(before_rotate[2][0]));

    const glm::mat4 stable_after_horizontal_scrub = pass->scene().camera.view;
    host.on_mouse_move({ kModAlt, { 520, 180 } });
    host.pump();

    const glm::mat4 after_vertical_alt_scrub = pass->scene().camera.view;
    CHECK(after_vertical_alt_scrub[0][0] == Catch::Approx(stable_after_horizontal_scrub[0][0]));
    CHECK(after_vertical_alt_scrub[2][0] == Catch::Approx(stable_after_horizontal_scrub[2][0]));

    host.on_mouse_button({ SDL_BUTTON_LEFT, false, kModAlt, { 520, 180 } });
    host.shutdown();
}

TEST_CASE("megacity host honors fractional mouse delta for drag input", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::MegaCity;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context(&window, &renderer, &text_service, std::move(launch), viewport, window.display_ppi_);

    REQUIRE(host.initialize(context, callbacks));
    host.attach_3d_renderer(renderer);

    auto* pass = dynamic_cast<IsometricScenePass*>(renderer.last_render_pass.get());
    REQUIRE(pass != nullptr);

    const glm::vec3 probe{ 0.5f, 0.0f, 0.5f };

    host.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 400, 300 } });

    const glm::vec2 before_pan = ndc_of_point(pass->scene(), probe);
    host.on_mouse_move({ kModNone, { 400, 300 }, { 18.5f, -10.25f } });
    host.pump();

    const glm::vec2 after_pan = ndc_of_point(pass->scene(), probe);
    CHECK(after_pan.x > before_pan.x);
    CHECK(after_pan.y > before_pan.y);

    const glm::mat4 before_rotate = pass->scene().camera.view;
    host.on_mouse_move({ kModAlt, { 400, 300 }, { 12.5f, 0.0f } });
    host.pump();
    pump_until_idle(host);

    const glm::mat4 after_rotate = pass->scene().camera.view;
    CHECK(after_rotate[0][0] != Catch::Approx(before_rotate[0][0]));
    CHECK(after_rotate[2][0] != Catch::Approx(before_rotate[2][0]));

    host.on_mouse_button({ SDL_BUTTON_LEFT, false, kModAlt, { 400, 300 } });
    host.shutdown();
}

TEST_CASE("megacity host keeps catching up between mouse samples", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::MegaCity;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context(&window, &renderer, &text_service, std::move(launch), viewport, window.display_ppi_);

    REQUIRE(host.initialize(context, callbacks));
    host.attach_3d_renderer(renderer);

    auto* pass = dynamic_cast<IsometricScenePass*>(renderer.last_render_pass.get());
    REQUIRE(pass != nullptr);

    const glm::vec3 probe{ 0.5f, 0.0f, 0.5f };

    host.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 400, 300 } });
    host.on_mouse_move({ kModNone, { 400, 300 }, { 40.0f, -24.0f } });
    host.pump();

    const glm::vec2 after_first_pump = ndc_of_point(pass->scene(), probe);
    const auto next_tick = host.next_deadline();
    REQUIRE(next_tick.has_value());

    std::this_thread::sleep_until(*next_tick);
    host.pump();

    const glm::vec2 after_second_pump = ndc_of_point(pass->scene(), probe);
    CHECK(after_second_pump.x > after_first_pump.x);
    CHECK(after_second_pump.y > after_first_pump.y);

    host.on_mouse_button({ SDL_BUTTON_LEFT, false, kModNone, { 400, 300 } });
    host.shutdown();
}

TEST_CASE("megacity mesh library builds expected primitive counts", "[megacity]")
{
    const MeshData cube = build_unit_cube_mesh();
    const MeshData floor = build_floor_box_mesh();
    const MeshData filled = build_grid_mesh(2, 2, 1.0f);

    FloorGridSpec grid;
    grid.enabled = true;
    grid.min_x = 0;
    grid.max_x = 2;
    grid.min_z = 0;
    grid.max_z = 2;
    grid.tile_size = 1.0f;
    grid.line_width = 0.04f;

    const MeshData outline = build_outline_grid_mesh(grid);

    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);

    CHECK(floor.vertices.size() == 24);
    CHECK(floor.indices.size() == 36);
    CHECK(triangle_up_normal_y(floor, 8) > 0.0f);

    CHECK(filled.vertices.size() == 16);
    CHECK(filled.indices.size() == 24);
    CHECK(triangle_up_normal_y(filled, 0) > 0.0f);

    CHECK(outline.vertices.size() == 24);
    CHECK(outline.indices.size() == 36);
    CHECK(triangle_up_normal_y(outline, 0) > 0.0f);
}

#endif

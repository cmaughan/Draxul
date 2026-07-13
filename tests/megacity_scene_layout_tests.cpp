#include "support/megacity_scene_test_support.h"

#ifdef DRAXUL_ENABLE_MEGACITY

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

    // Use explicit config values so the test is independent of struct defaults.
    MegaCityCodeConfig clamped_config;
    clamped_config.clamp_semantic_metrics = true;
    clamped_config.footprint_base = 1.0f;
    clamped_config.footprint_range = { 1.0f, 9.0f };
    clamped_config.footprint_unclamped_scale = 0.15f;
    clamped_config.height_base = 2.0f;
    clamped_config.height_mass_weight = 1.35f;
    clamped_config.height_count_weight = 0.45f;
    clamped_config.height_range = { 2.0f, 12.0f };
    clamped_config.height_unclamped_count_weight = 0.27f;
    clamped_config.road_width_base = 0.6f;
    clamped_config.road_width_scale = 0.85f;
    clamped_config.road_width_range = { 0.6f, 3.0f };

    MegaCityCodeConfig unclamped_config = clamped_config;
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
        row.is_struct,
        false,
        false,
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

TEST_CASE("semantic city layout can hide struct entities", "[megacity]")
{
    CityClassRecord class_row;
    class_row.name = "App";
    class_row.qualified_name = "App";
    class_row.module_path = "app";
    class_row.source_file_path = "app/app.cpp";
    class_row.entity_kind = "building";
    class_row.base_size = 8;
    class_row.building_functions = 4;
    class_row.function_sizes = { 20, 16 };
    class_row.road_size = 2;
    class_row.is_struct = false;

    CityClassRecord struct_row = class_row;
    struct_row.name = "AppState";
    struct_row.qualified_name = "AppState";
    struct_row.source_file_path = "app/app_state.h";
    struct_row.is_struct = true;

    const std::vector<CityClassRecord> rows{ class_row, struct_row };
    MegaCityCodeConfig visible_config;
    visible_config.clamp_semantic_metrics = true;
    visible_config.hide_struct_entities = false;
    MegaCityCodeConfig hidden_config = visible_config;
    hidden_config.hide_struct_entities = true;

    const SemanticCityLayout visible = build_semantic_city_layout(rows, visible_config);
    const SemanticCityLayout hidden = build_semantic_city_layout(rows, hidden_config);

    REQUIRE(visible.buildings.size() == 2);
    REQUIRE(hidden.buildings.size() == 1);
    CHECK(hidden.buildings[0].qualified_name == "App");
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

TEST_CASE("city routes dependencies through visible road space between buildings", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module_layout;
    module_layout.module_path = "libs/example";

    SemanticCityBuilding source;
    source.module_path = "libs/example";
    source.qualified_name = "Source";
    source.source_file_path = "libs/example/source.h";
    source.center = { 0.0f, 0.0f };
    source.metrics = {
        .footprint = 4.0f,
        .height = 8.0f,
        .sidewalk_width = 1.0f,
        .road_width = 3.0f,
    };

    SemanticCityBuilding target = source;
    target.qualified_name = "Target";
    target.source_file_path = "libs/example/target.h";
    target.center = { 8.0f, 8.0f };

    module_layout.buildings = { source, target };
    layout.modules.push_back(module_layout);
    layout.min_x = -6.0f;
    layout.max_x = 16.0f;
    layout.min_z = -6.0f;
    layout.max_z = 14.0f;

    SemanticMegacityModel model;
    model.modules.push_back({ module_layout.module_path, 0, 0.5f, {}, module_layout.buildings });
    model.dependencies.push_back({
        "libs/example",
        "Source",
        "target_",
        "Target",
        "libs/example",
        "Target",
        source.source_file_path,
        target.source_file_path,
    });

    MegaCityCodeConfig config;
    config.placement_step = 0.5f;
    const CityGrid grid = build_city_grid(layout, config);
    const auto routes = build_city_routes_for_selection(
        layout,
        model,
        grid,
        config,
        target.source_file_path,
        target.module_path,
        target.qualified_name);

    REQUIRE(routes.size() == 1);
    const auto& route = routes[0];
    REQUIRE(route.world_points.size() >= 4);
    CHECK(route.source_qualified_name == "Source");
    CHECK(route.target_qualified_name == "Target");
    CHECK(route.source_color == glm::vec4(0.20f, 0.88f, 0.30f, 1.0f));
    CHECK(route.target_color == glm::vec4(0.92f, 0.22f, 0.18f, 1.0f));

    bool found_diagonal = false;
    for (size_t i = 1; i < route.world_points.size(); ++i)
    {
        const glm::vec2 a = route.world_points[i - 1];
        const glm::vec2 b = route.world_points[i];
        found_diagonal |= std::abs(a.x - b.x) > 1e-4f && std::abs(a.y - b.y) > 1e-4f;
    }
    CHECK(found_diagonal);

    const auto point_in_sidewalk_or_building = [](const glm::vec2& point, const SemanticCityBuilding& building) {
        const float half_extent = building.metrics.footprint * 0.5f + building.metrics.sidewalk_width;
        return point.x > building.center.x - half_extent + 1e-4f
            && point.x < building.center.x + half_extent - 1e-4f
            && point.y > building.center.y - half_extent + 1e-4f
            && point.y < building.center.y + half_extent - 1e-4f;
    };

    for (size_t i = 1; i + 1 < route.world_points.size(); ++i)
    {
        const glm::vec2 point = route.world_points[i];
        CHECK_FALSE(point_in_sidewalk_or_building(point, source));
        CHECK_FALSE(point_in_sidewalk_or_building(point, target));
    }
}

TEST_CASE("selection routes allocate distinct target ports", "[megacity]")
{
    SemanticCityBuilding target;
    target.module_path = "libs/example";
    target.qualified_name = "Target";
    target.display_name = "Target";
    target.source_file_path = "libs/example/target.h";
    target.center = { 8.0f, 0.0f };
    target.metrics = { 4.0f, 6.0f, 1.0f, 1.0f };

    SemanticCityBuilding west = target;
    west.qualified_name = "West";
    west.display_name = "West";
    west.source_file_path = "libs/example/west.h";
    west.center = { 0.0f, 0.0f };

    SemanticCityBuilding north = target;
    north.qualified_name = "North";
    north.display_name = "North";
    north.source_file_path = "libs/example/north.h";
    north.center = { 8.0f, 8.0f };

    SemanticCityBuilding east = target;
    east.qualified_name = "East";
    east.display_name = "East";
    east.source_file_path = "libs/example/east.h";
    east.center = { 16.0f, 0.0f };

    SemanticCityModuleLayout module_layout;
    module_layout.module_path = "libs/example";
    module_layout.buildings = { west, north, east, target };

    SemanticMegacityLayout layout;
    layout.modules.push_back(module_layout);
    layout.min_x = -4.0f;
    layout.max_x = 20.0f;
    layout.min_z = -4.0f;
    layout.max_z = 12.0f;

    SemanticMegacityModel model;
    model.modules.push_back({ module_layout.module_path, 0, 0.5f, {}, module_layout.buildings });
    model.dependencies.push_back({
        "libs/example",
        "West",
        "target_",
        "Target",
        "libs/example",
        "Target",
        west.source_file_path,
        target.source_file_path,
    });
    model.dependencies.push_back({
        "libs/example",
        "North",
        "target_",
        "Target",
        "libs/example",
        "Target",
        north.source_file_path,
        target.source_file_path,
    });
    model.dependencies.push_back({
        "libs/example",
        "East",
        "target_",
        "Target",
        "libs/example",
        "Target",
        east.source_file_path,
        target.source_file_path,
    });

    MegaCityCodeConfig config;
    config.placement_step = 0.5f;
    const CityGrid grid = build_city_grid(layout, config);
    const auto routes = build_city_routes_for_selection(
        layout,
        model,
        grid,
        config,
        target.source_file_path,
        target.module_path,
        target.qualified_name);

    REQUIRE(routes.size() == 3);
    REQUIRE(routes[0].world_points.size() >= 2);
    REQUIRE(routes[1].world_points.size() >= 2);
    REQUIRE(routes[2].world_points.size() >= 2);

    const glm::vec2 end_a = routes[0].world_points.back();
    const glm::vec2 end_b = routes[1].world_points.back();
    const glm::vec2 end_c = routes[2].world_points.back();

    CHECK(glm::distance(end_a, end_b) > 1e-3f);
    CHECK(glm::distance(end_a, end_c) > 1e-3f);
    CHECK(glm::distance(end_b, end_c) > 1e-3f);
}

TEST_CASE("route render segments preserve independent route geometry", "[megacity]")
{
    std::vector<CityGrid::RoutePolyline> routes;
    routes.push_back({
        "libs/example/alpha.h",
        "libs/example",
        "Alpha",
        "libs/example/target.h",
        "libs/example",
        "Target",
        {},
        {},
        glm::vec4(0.20f, 0.88f, 0.30f, 1.0f),
        glm::vec4(0.92f, 0.22f, 0.18f, 1.0f),
        {
            { 0.0f, -1.0f },
            { 0.0f, 0.0f },
            { 2.0f, 0.0f },
            { 4.0f, 0.0f },
            { 5.0f, 0.0f },
        },
    });
    routes.push_back({
        "libs/example/beta.h",
        "libs/example",
        "Beta",
        "libs/example/target.h",
        "libs/example",
        "Target",
        {},
        {},
        glm::vec4(0.20f, 0.88f, 0.30f, 1.0f),
        glm::vec4(0.92f, 0.22f, 0.18f, 1.0f),
        {
            { 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 2.0f, 0.0f },
            { 4.0f, 0.0f },
            { 5.0f, 0.0f },
        },
    });

    const auto segments = build_city_route_render_segments(routes, 0.2f);
    REQUIRE(segments.size() == 8);

    bool found_centerline_gradient_segment = false;
    bool found_greenish_segment = false;
    bool found_reddish_segment = false;
    for (const auto& segment : segments)
    {
        if (std::abs(segment.a.y) <= 1e-4f && std::abs(segment.b.y) <= 1e-4f
            && segment.a.x >= 2.0f - 1e-4f && segment.b.x <= 4.0f + 1e-4f)
        {
            found_centerline_gradient_segment = true;
        }
        found_greenish_segment |= segment.color.g > segment.color.r;
        found_reddish_segment |= segment.color.r > segment.color.g;
    }

    CHECK(found_centerline_gradient_segment);
    CHECK(found_greenish_segment);
    CHECK(found_reddish_segment);
}

TEST_CASE("selection routes distinguish duplicate names in the same module by source file", "[megacity]")
{
    SemanticCityBuilding left;
    left.module_path = "tests";
    left.qualified_name = "FakeGlyphAtlas";
    left.display_name = "FakeGlyphAtlas";
    left.source_file_path = "tests/font_size_tests.cpp";
    left.center = { -4.0f, 0.0f };
    left.metrics = { 2.5f, 4.0f, 1.0f, 1.0f };

    SemanticCityBuilding right = left;
    right.source_file_path = "tests/grid_rendering_pipeline_tests.cpp";
    right.center = { 4.0f, 0.0f };

    SemanticCityBuilding target = left;
    target.qualified_name = "IGlyphAtlas";
    target.display_name = "IGlyphAtlas";
    target.source_file_path = "libs/draxul-font/include/draxul/iglyph_atlas.h";
    target.center = { 0.0f, 8.0f };

    SemanticCityModuleLayout module_layout;
    module_layout.module_path = "tests";
    module_layout.buildings = { left, right, target };

    SemanticMegacityLayout layout;
    layout.modules.push_back(module_layout);
    layout.min_x = -8.0f;
    layout.max_x = 8.0f;
    layout.min_z = -4.0f;
    layout.max_z = 12.0f;

    SemanticMegacityModel model;
    model.modules.push_back({ module_layout.module_path, 0, 0.5f, {}, module_layout.buildings });
    model.dependencies.push_back({
        right.module_path,
        right.qualified_name,
        "impl_",
        target.qualified_name,
        target.module_path,
        target.qualified_name,
        right.source_file_path,
        target.source_file_path,
    });

    MegaCityCodeConfig config;
    config.placement_step = 0.5f;
    const CityGrid grid = build_city_grid(layout, config);

    const auto left_routes = build_city_routes_for_selection(
        layout,
        model,
        grid,
        config,
        left.source_file_path,
        left.module_path,
        left.qualified_name);
    CHECK(left_routes.empty());

    const auto right_routes = build_city_routes_for_selection(
        layout,
        model,
        grid,
        config,
        right.source_file_path,
        right.module_path,
        right.qualified_name);
    REQUIRE(right_routes.size() == 1);
    CHECK(right_routes[0].source_file_path == right.source_file_path);
    CHECK(right_routes[0].source_qualified_name == right.qualified_name);
    CHECK(right_routes[0].target_file_path == target.source_file_path);
}

TEST_CASE("roof sign mesh textures only the top face", "[megacity]")
{
    const MeshData mesh = build_top_label_panel_mesh();

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
    const MeshData mesh = build_front_label_panel_mesh();

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
    const float park_lot_half = layout.park_footprint * 0.5f
        + layout.park_sidewalk_width + layout.park_road_width;
    const TestLotRect park_lot{
        layout.park_center.x - park_lot_half,
        layout.park_center.x + park_lot_half,
        layout.park_center.y - park_lot_half,
        layout.park_center.y + park_lot_half,
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

TEST_CASE("megacity mesh library builds expected primitive counts", "[megacity]")
{
    const MeshData cube = build_unit_cube_mesh();
    const MeshData floor = build_floor_box_mesh();
    const MeshData foliage_stem = build_foliage_stem_mesh();
    const MeshData foliage_card = build_foliage_card_mesh();
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

    CHECK_FALSE(foliage_stem.vertices.empty());
    CHECK_FALSE(foliage_stem.indices.empty());
    CHECK(foliage_stem.indices.size() % 3 == 0);
    CHECK_FALSE(foliage_card.vertices.empty());
    CHECK_FALSE(foliage_card.indices.empty());
    CHECK(foliage_card.indices.size() % 3 == 0);
    float tree_max_y = 0.0f;
    for (const auto& vertex : foliage_stem.vertices)
        tree_max_y = std::max(tree_max_y, vertex.position.y);
    for (const auto& vertex : foliage_card.vertices)
        tree_max_y = std::max(tree_max_y, vertex.position.y);
    CHECK(tree_max_y >= Catch::Approx(7.0f).margin(0.01f));

    CHECK(filled.vertices.size() == 16);
    CHECK(filled.indices.size() == 24);
    CHECK(triangle_up_normal_y(filled, 0) > 0.0f);

    CHECK(outline.vertices.size() == 24);
    CHECK(outline.indices.size() == 36);
    CHECK(triangle_up_normal_y(outline, 0) > 0.0f);
}

#endif

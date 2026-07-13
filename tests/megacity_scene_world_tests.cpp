#include "support/megacity_scene_test_support.h"

#ifdef DRAXUL_ENABLE_MEGACITY

TEST_CASE("megacity world maps grid coordinates to tile centers", "[megacity]")
{
    CodeVizSceneWorld world;

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
    CodeVizSceneWorld world;

    const auto view = world.registry().view<const Appearance>();
    CHECK(view.begin() == view.end());
}

TEST_CASE("megacity world creates bark and leaf tree entities", "[megacity]")
{
    CodeVizSceneWorld world;
    const TreeMetrics metrics{
        .height = 7.0f,
        .canopy_radius = 1.6f,
    };

    const entt::entity bark_entity = world.create_tree_bark(
        2.0f,
        3.0f,
        0.25f,
        metrics,
        glm::vec4(1.0f),
        CodeVizSemanticRef{ "", "CentralParkTreeBark" });
    const entt::entity leaf_entity = world.create_tree_leaves(
        2.0f,
        3.0f,
        0.25f,
        metrics,
        glm::vec4(1.0f),
        CodeVizSemanticRef{ "", "CentralParkTreeLeaves" });

    const auto& bark_appearance = world.registry().get<Appearance>(bark_entity);
    const auto& leaf_appearance = world.registry().get<Appearance>(leaf_entity);
    const auto& stored_metrics = world.registry().get<TreeMetrics>(bark_entity);
    const auto& elevation = world.registry().get<Elevation>(bark_entity);

    CHECK(bark_appearance.mesh == kCityTreeBarkMesh);
    CHECK(bark_appearance.material == kCityTreeBarkMaterial);
    CHECK_FALSE(bark_appearance.double_sided);
    CHECK(leaf_appearance.mesh == kCityTreeLeavesMesh);
    CHECK(leaf_appearance.material == kCityLeafCardMaterial);
    CHECK_FALSE(leaf_appearance.double_sided);
    CHECK(stored_metrics.height == Catch::Approx(7.0f));
    CHECK(stored_metrics.canopy_radius == Catch::Approx(1.6f));
    CHECK(elevation.value == Catch::Approx(0.25f));
}

TEST_CASE("MegaCity build_city consumes a neutral semantic snapshot", "[megacity]")
{
    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "app/widget.cpp";
    file.symbols.push_back(SymbolRecord{
        SymbolKind::Class,
        "Widget",
        "",
        false,
        1,
        20,
        2,
        {},
        {},
    });
    file.symbols.push_back(SymbolRecord{
        SymbolKind::Function,
        "draw",
        "Widget",
        false,
        10,
        16,
    });
    snapshot.files.push_back(std::move(file));

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);
    CodeVizSceneWorld world;

    MegaCityCodeConfig config;
    uint64_t sign_revision = 0;
    const CityBuildResult result = build_city(
        world, semantics, nullptr, config, sign_revision);

    REQUIRE(result.semantic_model);
    REQUIRE(result.semantic_model->modules.size() == 1);
    REQUIRE(result.semantic_model->modules[0].module_path == "app");
    REQUIRE(result.semantic_model->modules[0].buildings.size() == 1);
    REQUIRE(result.semantic_model->modules[0].buildings[0].qualified_name == "Widget");
}

TEST_CASE("BioView analysis panel does not expose city-only controls", "[megacity][bioview]")
{
    const CodeVisualizationPanelCapabilities city
        = code_visualization_panel_capabilities(CodeVisualizationPanelMode::City);
    const CodeVisualizationPanelCapabilities biology
        = code_visualization_panel_capabilities(CodeVisualizationPanelMode::Biology);

    CHECK(city.show_city_build_controls);
    CHECK(city.show_city_sign_controls);
    CHECK(city.show_city_surface_controls);
    CHECK(city.show_city_preview);
    CHECK(city.show_perf_overlay_controls);
    CHECK(city.show_perf_debug);
    CHECK_FALSE(city.show_biology_build_controls);

    CHECK(biology.show_biology_build_controls);
    CHECK_FALSE(biology.show_city_build_controls);
    CHECK_FALSE(biology.show_city_sign_controls);
    CHECK_FALSE(biology.show_city_surface_controls);
    CHECK_FALSE(biology.show_city_preview);
    CHECK_FALSE(biology.show_perf_overlay_controls);
    CHECK_FALSE(biology.show_perf_debug);
}

TEST_CASE("BioView maps the most significant class onto cell organelles", "[megacity][bioview]")
{
    CodeVizSceneWorld world;
    CodebaseSnapshot snapshot;
    snapshot.complete = true;

    ParsedFile widget_file;
    widget_file.path = "app/widget.cpp";

    // The hero class: 4 methods (one oversized -> lysosome) and 2 fields.
    SymbolRecord widget;
    widget.kind = SymbolKind::Class;
    widget.name = "Widget";
    widget.line = 3;
    widget.end_line = 140;
    widget.field_count = 2;
    widget.fields.push_back(SymbolRecord::FieldRecord{ "config_", "WidgetConfig", { "WidgetConfig" } });
    widget.fields.push_back(SymbolRecord::FieldRecord{ "buffer_", "Buffer", {} });

    auto make_method = [](const char* name, uint32_t line, uint32_t end_line) {
        SymbolRecord method;
        method.kind = SymbolKind::Function;
        method.name = name;
        method.parent = "Widget";
        method.line = line;
        method.end_line = end_line;
        return method;
    };
    const SymbolRecord draw = make_method("draw", 10, 96); // oversized -> lysosome
    const SymbolRecord resize = make_method("resize", 98, 108);
    const SymbolRecord update = make_method("update", 110, 122);
    const SymbolRecord paint = make_method("paint", 124, 138);

    // A smaller struct that should NOT be selected as the hero cell.
    SymbolRecord config;
    config.kind = SymbolKind::Struct;
    config.name = "WidgetConfig";
    config.line = 1;
    config.end_line = 4;
    config.field_count = 1;
    config.fields.push_back(SymbolRecord::FieldRecord{ "value", "int", {} });

    widget_file.symbols = { widget, draw, resize, update, paint, config };
    snapshot.files.push_back(std::move(widget_file));

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);

    MegaCityCodeConfig cfg;
    const BiologyBuildResult result = build_biology_view(world, semantics, cfg);

    // Both types become cells (one module tissue); Widget is the hero.
    CHECK(result.mapped_from_semantics);
    CHECK(result.subject_label == "Widget");
    CHECK(result.bounds_valid);
    CHECK(result.computed_default_light);
    CHECK(result.stats.module_tissue_count == 1);
    CHECK(result.stats.cell_count == 2); // Widget + WidgetConfig
    CHECK(result.stats.full_cell_count == 2); // both fit within the full-detail budget

    // Members of the full cells map onto organelles.
    CHECK(result.stats.mitochondria_count == 4); // Widget's 4 methods (WidgetConfig has none)
    CHECK(result.stats.ribosome_count == 3); // Widget's 2 fields + WidgetConfig's 1

    // Double-sided translucent shells (cell membranes + nuclear envelopes).
    size_t double_sided_shells = 0;
    auto ellipsoid_view = world.registry().view<const EllipsoidMetrics, const Appearance>();
    for (const entt::entity entity : ellipsoid_view)
    {
        const auto& appearance = ellipsoid_view.get<const Appearance>(entity);
        if (appearance.double_sided && appearance.color.a < 1.0f)
            ++double_sided_shells;
    }
    CHECK(double_sided_shells >= 2);

    // Organelles carry semantic refs back to the class's real members.
    bool found_method_ref = false;
    bool found_field_ref = false;
    auto ref_view = world.registry().view<const CodeVizSemanticRef>();
    for (const entt::entity entity : ref_view)
    {
        const auto& ref = ref_view.get<const CodeVizSemanticRef>(entity);
        if (ref.name == "Widget::draw" || ref.name == "Widget::paint")
            found_method_ref = true;
        if (ref.name == "Widget::config_" || ref.name == "Widget::buffer_")
            found_field_ref = true;
    }
    CHECK(found_method_ref);
    CHECK(found_field_ref);

    // Deterministic for a fixed snapshot.
    CodeVizSceneWorld world_again;
    const BiologyBuildResult repeat = build_biology_view(world_again, semantics, cfg);
    CHECK(repeat.subject_label == result.subject_label);
    CHECK(repeat.stats.cell_count == result.stats.cell_count);
    CHECK(repeat.stats.mitochondria_count == result.stats.mitochondria_count);

    IsometricCamera camera;
    camera.set_viewport(800, 600);
    const CodeVizSceneSnapshotResult scene = build_scene_snapshot(
        camera, world, cfg, nullptr, {}, nullptr, nullptr);
    CHECK(scene.snapshot.objects.size() >= 18);
    // Translucent membranes/vesicles produce a transparent tail after the opaque set.
    CHECK(scene.snapshot.opaque_count < scene.snapshot.objects.size());
}

TEST_CASE("BioView grows module tissues joined by dependency blood vessels", "[megacity][bioview]")
{
    CodeVizSceneWorld world;
    CodebaseSnapshot snapshot;
    snapshot.complete = true;

    // Module "app": a class whose fields reach into "libs/core" three times.
    ParsedFile widget_file;
    widget_file.path = "app/widget.cpp";
    SymbolRecord widget;
    widget.kind = SymbolKind::Class;
    widget.name = "Widget";
    widget.line = 1;
    widget.end_line = 40;
    widget.field_count = 3;
    widget.fields.push_back(SymbolRecord::FieldRecord{ "a", "Alpha", { "Alpha" } });
    widget.fields.push_back(SymbolRecord::FieldRecord{ "b", "Beta", { "Beta" } });
    widget.fields.push_back(SymbolRecord::FieldRecord{ "c", "Gamma", { "Gamma" } });
    SymbolRecord run;
    run.kind = SymbolKind::Function;
    run.name = "run";
    run.parent = "Widget";
    run.line = 10;
    run.end_line = 30;
    widget_file.symbols = { widget, run };
    snapshot.files.push_back(std::move(widget_file));

    // Module "libs/core": the referenced types.
    ParsedFile core_file;
    core_file.path = "libs/core/types.h";
    for (const char* name : { "Alpha", "Beta", "Gamma" })
    {
        SymbolRecord type;
        type.kind = SymbolKind::Struct;
        type.name = name;
        type.line = 1;
        type.end_line = 3;
        core_file.symbols.push_back(type);
    }
    snapshot.files.push_back(std::move(core_file));

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);
    MegaCityCodeConfig cfg;
    const BiologyBuildResult result = build_biology_view(world, semantics, cfg);

    CHECK(result.stats.module_tissue_count == 2); // app + libs/core
    CHECK(result.stats.cell_count == 4); // Widget + Alpha/Beta/Gamma
    CHECK(result.stats.vessel_count >= 1); // 3 cross-module references

    // The vessel is a baked custom mesh; tissue patches are ellipsoids.
    auto tissue_view = world.registry().view<const EllipsoidMetrics, const CodeVizSemanticRef>();
    size_t tissue_patches = 0;
    for (const entt::entity entity : tissue_view)
    {
        const auto& ref = tissue_view.get<const CodeVizSemanticRef>(entity);
        if (ref.semantic_node_id == 0 && !ref.module_path.empty() && ref.name == ref.module_path)
            ++tissue_patches;
    }
    CHECK(tissue_patches == 2);
}

TEST_CASE("BioView falls back to a generic cell when there are no types", "[megacity][bioview]")
{
    CodeVizSceneWorld world;
    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    ParsedFile file;
    file.path = "app/main.cpp";
    SymbolRecord free_function;
    free_function.kind = SymbolKind::Function;
    free_function.name = "main";
    free_function.line = 1;
    free_function.end_line = 20;
    file.symbols = { free_function };
    snapshot.files.push_back(std::move(file));

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);
    MegaCityCodeConfig cfg;
    const BiologyBuildResult result = build_biology_view(world, semantics, cfg);

    CHECK_FALSE(result.mapped_from_semantics);
    CHECK(result.subject_label.empty());
    CHECK(result.bounds_valid);
    CHECK(result.stats.cell_count == 1);
    CHECK(result.stats.mitochondria_count > 0);

    auto ellipsoid_view = world.registry().view<const EllipsoidMetrics>();
    CHECK(ellipsoid_view.begin() != ellipsoid_view.end());
}

TEST_CASE("megacity world creates module surface entities", "[megacity]")
{
    CodeVizSceneWorld world;

    const entt::entity entity = world.create_module_surface(
        4.0f,
        6.0f,
        ModuleSurfaceMetrics{
            .extent_x = 10.0f,
            .extent_z = 14.0f,
            .height = 0.018f,
        },
        glm::vec4(0.2f, 0.4f, 0.8f, 1.0f),
        CodeVizSemanticRef{ "", "libs/example" },
        0.05f);

    const auto& appearance = world.registry().get<Appearance>(entity);
    const auto& metrics = world.registry().get<ModuleSurfaceMetrics>(entity);
    const auto& elevation = world.registry().get<Elevation>(entity);

    CHECK(appearance.mesh == CodeVizMeshId::Cube);
    CHECK(appearance.material == CodeVizMaterialPreset::FlatColor);
    CHECK(metrics.extent_x == Catch::Approx(10.0f));
    CHECK(metrics.extent_z == Catch::Approx(14.0f));
    CHECK(metrics.height == Catch::Approx(0.018f));
    CHECK(elevation.value == Catch::Approx(0.05f));
}

TEST_CASE("megacity module signs are placed on module border strips", "[megacity]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "src/example.cpp";
    file.symbols.push_back(SymbolRecord{
        SymbolKind::Class,
        "Tower",
        "",
        false,
        1,
        24,
        1,
        {},
        {
            { "count_", "int", {} },
        },
    });
    snapshot.files.push_back(file);

    CodeVizSceneWorld world;
    MegaCityCodeConfig config;
    uint64_t sign_label_revision = 1;
    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);

    CityBuildResult build = build_city(
        world,
        semantics,
        &text_service,
        config,
        sign_label_revision);

    REQUIRE(build.layout);
    const auto module_it = std::find_if(
        build.layout->modules.begin(),
        build.layout->modules.end(),
        [](const SemanticCityModuleLayout& module) { return module.module_path == "src"; });
    REQUIRE(module_it != build.layout->modules.end());
    const SemanticCityModuleLayout& module_layout = *module_it;
    const float extent_x = module_layout.max_x - module_layout.min_x;
    const float extent_z = module_layout.max_z - module_layout.min_z;
    REQUIRE(extent_x > 0.0f);
    REQUIRE(extent_z > 0.0f);

    std::vector<glm::vec2> horizontal_border_centers;
    auto border_view = world.registry().view<const ModuleSurfaceMetrics, const WorldPosition, const CodeVizSemanticRef>();
    for (const entt::entity entity : border_view)
    {
        const auto& metrics = border_view.get<const ModuleSurfaceMetrics>(entity);
        const auto& position = border_view.get<const WorldPosition>(entity);
        const auto& source = border_view.get<const CodeVizSemanticRef>(entity);
        if (source.module_path != module_layout.module_path || source.name != module_layout.module_path)
            continue;
        if (metrics.extent_x <= metrics.extent_z)
            continue;
        horizontal_border_centers.emplace_back(position.x, position.z);
    }
    REQUIRE(horizontal_border_centers.size() == 2);

    std::vector<glm::vec2> sign_centers;
    std::vector<float> sign_widths;
    std::vector<float> sign_heights;
    std::vector<float> sign_depths;
    std::vector<glm::vec4> sign_colors;
    auto sign_view = world.registry().view<const SignMetrics, const WorldPosition, const CodeVizSemanticRef, const Appearance>();
    for (const entt::entity entity : sign_view)
    {
        const auto& metrics = sign_view.get<const SignMetrics>(entity);
        const auto& position = sign_view.get<const WorldPosition>(entity);
        const auto& source = sign_view.get<const CodeVizSemanticRef>(entity);
        const auto& appearance = sign_view.get<const Appearance>(entity);
        if (source.file.empty()
            && source.module_path == module_layout.module_path
            && source.name == module_layout.module_path)
        {
            sign_centers.emplace_back(position.x, position.z);
            sign_widths.push_back(metrics.width);
            sign_heights.push_back(metrics.height);
            sign_depths.push_back(metrics.depth);
            sign_colors.push_back(appearance.color);
        }
    }
    REQUIRE(sign_centers.size() == 2);
    REQUIRE(sign_widths.size() == 2);
    REQUIRE(sign_heights.size() == 2);
    REQUIRE(sign_depths.size() == 2);
    REQUIRE(sign_colors.size() == 2);

    for (size_t index = 0; index < sign_centers.size(); ++index)
    {
        const glm::vec2& sign_center = sign_centers[index];
        const bool matches_border = std::any_of(
            horizontal_border_centers.begin(),
            horizontal_border_centers.end(),
            [&](const glm::vec2& border_center) {
                return border_center.x == Catch::Approx(sign_center.x).margin(1e-4f);
            });
        CHECK(matches_border);
        const float expected_north_z = module_layout.max_z - sign_depths[index] * 0.5f;
        const float expected_south_z = module_layout.min_z + sign_depths[index] * 0.5f;
        const bool matches_inset_edge
            = sign_center.y == Catch::Approx(expected_north_z).margin(1e-4f)
            || sign_center.y == Catch::Approx(expected_south_z).margin(1e-4f);
        CHECK(matches_inset_edge);
    }

    for (const float sign_width : sign_widths)
        CHECK(sign_width == Catch::Approx(module_layout.park_footprint).margin(1e-4f));
    for (const float sign_depth : sign_depths)
        CHECK(sign_depth == Catch::Approx(config.roof_sign_thickness * 0.5f).margin(1e-4f));
    for (size_t index = 0; index < sign_heights.size(); ++index)
        CHECK(sign_heights[index] > sign_depths[index]);
    const glm::vec4 expected_sign_color = glm::vec4(
        glm::clamp(
            glm::mix(glm::vec3(module_building_color(module_layout.module_path)), kCatppuccinSurface0, 0.45f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)),
        module_building_color(module_layout.module_path).a);
    for (const glm::vec4& sign_color : sign_colors)
    {
        CHECK(sign_color.r == Catch::Approx(expected_sign_color.r).margin(1e-4f));
        CHECK(sign_color.g == Catch::Approx(expected_sign_color.g).margin(1e-4f));
        CHECK(sign_color.b == Catch::Approx(expected_sign_color.b).margin(1e-4f));
        CHECK(sign_color.a == Catch::Approx(expected_sign_color.a).margin(1e-4f));
    }

    text_service.shutdown();
}

TEST_CASE("megacity live metrics snapshot includes buildings and functions", "[megacity]")
{
    SemanticMegacityModel model;
    SemanticCityModuleModel module;
    module.module_path = "app";

    SemanticCityBuilding building;
    building.module_path = "app";
    building.display_name = "Renderer";
    building.qualified_name = "Renderer";
    building.source_file_path = "app/renderer.cpp";
    building.layers = {
        { "update", "", 10, 1.0f },
        { "render", "", 20, 2.0f },
        { "present", "", 30, 3.0f },
    };
    module.buildings.push_back(building);
    model.modules.push_back(module);

    RuntimePerfSnapshot perf_snapshot;
    perf_snapshot.generation = 7;
    perf_snapshot.frame_time_microseconds = 10000;
    perf_snapshot.functions.push_back({
        .source_file_path = "app/renderer.cpp",
        .owner_qualified_name = "Renderer",
        .function_name = "update",
        .pretty_function = "void draxul::Renderer::update()",
        .frame_microseconds = 2500,
        .smoothed_microseconds = 2500,
        .frame_fraction = 0.25f,
        .smoothed_frame_fraction = 0.25f,
        .normalized_heat = 0.5f,
        .call_count = 1,
    });
    perf_snapshot.functions.push_back({
        .source_file_path = "app/renderer.cpp",
        .owner_qualified_name = "Renderer",
        .function_name = "render",
        .pretty_function = "void draxul::Renderer::render()",
        .frame_microseconds = 5000,
        .smoothed_microseconds = 5000,
        .frame_fraction = 0.5f,
        .smoothed_frame_fraction = 0.5f,
        .normalized_heat = 1.0f,
        .call_count = 1,
    });

    const LiveCityMetricsSnapshot snapshot = build_live_city_metrics_snapshot(model, &perf_snapshot);

    REQUIRE(snapshot.generation == 7);
    REQUIRE(snapshot.buildings.size() == 1);
    REQUIRE(snapshot.functions.size() == 3);
    CHECK(snapshot.buildings[0].qualified_name == "Renderer");
    CHECK(snapshot.buildings[0].frame_fraction == Catch::Approx(0.5f));
    CHECK(snapshot.buildings[0].smoothed_frame_fraction == Catch::Approx(0.5f));
    CHECK(snapshot.buildings[0].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[0].function_name == "update");
    CHECK(snapshot.functions[0].heat == Catch::Approx(0.5f));
    CHECK(snapshot.functions[1].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[2].heat == Catch::Approx(0.0f));
}

TEST_CASE("megacity live metrics snapshot matches header-owned buildings to implementation timings", "[megacity]")
{
    SemanticMegacityModel model;
    SemanticCityModuleModel module;
    module.module_path = "libs/draxul-renderer";

    SemanticCityBuilding building;
    building.module_path = "libs/draxul-renderer";
    building.display_name = "MetalRenderer";
    building.qualified_name = "MetalRenderer";
    building.source_file_path = "libs/draxul-renderer/src/metal/metal_renderer.h";
    building.layers = {
        { "begin_frame", "", 10, 1.0f },
        { "end_frame", "", 20, 1.0f },
    };
    module.buildings.push_back(building);
    model.modules.push_back(module);

    RuntimePerfSnapshot perf_snapshot;
    perf_snapshot.generation = 3;
    perf_snapshot.frame_time_microseconds = 10000;
    perf_snapshot.functions.push_back({
        .source_file_path = "libs/draxul-renderer/src/metal/metal_renderer.mm",
        .owner_qualified_name = "MetalRenderer",
        .function_name = "begin_frame",
        .pretty_function = "bool draxul::MetalRenderer::begin_frame()",
        .frame_microseconds = 5000,
        .smoothed_microseconds = 5000,
        .frame_fraction = 0.5f,
        .smoothed_frame_fraction = 0.5f,
        .normalized_heat = 1.0f,
        .call_count = 1,
    });

    const LiveCityMetricsSnapshot snapshot = build_live_city_metrics_snapshot(model, &perf_snapshot);

    REQUIRE(snapshot.buildings.size() == 1);
    REQUIRE(snapshot.functions.size() == 2);
    CHECK(snapshot.buildings[0].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[0].function_name == "begin_frame");
    CHECK(snapshot.functions[0].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[1].function_name == "end_frame");
    CHECK(snapshot.functions[1].heat == Catch::Approx(0.0f));
}

TEST_CASE("megacity live metrics snapshot coverage mode lights touched functions fully", "[megacity]")
{
    SemanticMegacityModel model;
    SemanticCityModuleModel module;
    module.module_path = "app";

    SemanticCityBuilding building;
    building.module_path = "app";
    building.display_name = "Renderer";
    building.qualified_name = "Renderer";
    building.source_file_path = "app/renderer.cpp";
    building.layers = {
        { "update", "", 10, 1.0f },
        { "render", "", 20, 2.0f },
        { "present", "", 30, 3.0f },
    };
    module.buildings.push_back(building);
    model.modules.push_back(module);

    RuntimePerfSnapshot perf_snapshot;
    perf_snapshot.generation = 9;
    perf_snapshot.frame_time_microseconds = 10000;
    perf_snapshot.functions.push_back({
        .source_file_path = "app/renderer.cpp",
        .owner_qualified_name = "Renderer",
        .function_name = "update",
        .pretty_function = "void draxul::Renderer::update()",
        .frame_microseconds = 2500,
        .smoothed_microseconds = 2500,
        .frame_fraction = 0.25f,
        .smoothed_frame_fraction = 0.25f,
        .normalized_heat = 0.02f,
        .call_count = 1,
    });

    const LiveCityMetricsSnapshot snapshot = build_live_city_metrics_snapshot(model, &perf_snapshot, true);

    REQUIRE(snapshot.buildings.size() == 1);
    REQUIRE(snapshot.functions.size() == 3);
    CHECK(snapshot.buildings[0].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[0].heat == Catch::Approx(1.0f));
    CHECK(snapshot.functions[1].heat == Catch::Approx(0.0f));
    CHECK(snapshot.functions[2].heat == Catch::Approx(0.0f));
}

TEST_CASE("megacity perf debug state reports matched and unmatched runtime functions", "[megacity]")
{
    SemanticMegacityModel model;
    SemanticCityModuleModel module;
    module.module_path = "libs/draxul-renderer";

    SemanticCityBuilding building;
    building.module_path = "libs/draxul-renderer";
    building.display_name = "MetalRenderer";
    building.qualified_name = "MetalRenderer";
    building.source_file_path = "libs/draxul-renderer/src/metal/metal_renderer.h";
    building.layers = {
        { "begin_frame", "", 10, 1.0f },
        { "end_frame", "", 20, 1.0f },
    };
    module.buildings.push_back(building);
    model.modules.push_back(module);

    RuntimePerfSnapshot perf_snapshot;
    perf_snapshot.generation = 11;
    perf_snapshot.frame_index = 17;
    perf_snapshot.frame_time_microseconds = 10000;
    perf_snapshot.functions.push_back({
        .source_file_path = "libs/draxul-renderer/src/metal/metal_renderer.mm",
        .owner_qualified_name = "MetalRenderer",
        .function_name = "begin_frame",
        .pretty_function = "bool draxul::MetalRenderer::begin_frame()",
        .frame_microseconds = 5000,
        .smoothed_microseconds = 5000,
        .frame_fraction = 0.5f,
        .smoothed_frame_fraction = 0.5f,
        .normalized_heat = 1.0f,
        .call_count = 1,
    });
    perf_snapshot.functions.push_back({
        .source_file_path = "libs/draxul-renderer/src/metal/metal_renderer.mm",
        .owner_qualified_name = "MetalRenderer",
        .function_name = "unmatched_helper",
        .pretty_function = "void draxul::MetalRenderer::unmatched_helper()",
        .frame_microseconds = 1000,
        .smoothed_microseconds = 1000,
        .frame_fraction = 0.1f,
        .smoothed_frame_fraction = 0.1f,
        .normalized_heat = 0.2f,
        .call_count = 1,
    });

    const LiveCityPerfDebugState debug = build_live_city_perf_debug_state(model, &perf_snapshot);

    CHECK(debug.generation == 11);
    CHECK(debug.frame_index == 17);
    CHECK(debug.semantic_building_count == 1u);
    CHECK(debug.semantic_layer_count == 2u);
    CHECK(debug.runtime_function_count == 2u);
    CHECK(debug.matched_runtime_function_count == 1u);
    CHECK(debug.matched_layer_count == 1u);
    CHECK(debug.heated_layer_count == 1u);
    CHECK(debug.heated_building_count == 1u);
    REQUIRE(debug.top_matched_functions.size() == 1u);
    CHECK(debug.top_matched_functions[0].function_name == "begin_frame");
    REQUIRE(debug.top_unmatched_functions.size() == 1u);
    CHECK(debug.top_unmatched_functions[0].function_name == "unmatched_helper");
}

TEST_CASE("megacity scene snapshot carries per-layer performance heat state for buildings", "[megacity]")
{
    CodeVizSceneWorld world;
    world.create_building(
        2.0f,
        3.0f,
        0.0f,
        BuildingMetrics{
            .footprint = 2.0f,
            .height = 6.0f,
            .sidewalk_width = 0.0f,
            .road_width = 0.0f,
        },
        glm::vec4(1.0f),
        CodeVizSemanticRef{ "app/renderer.cpp", "Renderer", "app" },
        CodeVizMaterialPreset::FlatColor);

    auto live_metrics = std::make_shared<LiveCityMetricsSnapshot>();
    live_metrics->buildings.push_back({
        .source_file_path = "app/renderer.cpp",
        .module_path = "app",
        .qualified_name = "Renderer",
        .display_name = "Renderer",
        .heat = 0.4f,
    });
    live_metrics->functions.push_back({
        .source_file_path = "app/renderer.cpp",
        .module_path = "app",
        .qualified_name = "Renderer",
        .function_name = "update",
        .layer_index = 0,
        .layer_count = 2,
        .heat = 0.1f,
    });
    live_metrics->functions.push_back({
        .source_file_path = "app/renderer.cpp",
        .module_path = "app",
        .qualified_name = "Renderer",
        .function_name = "render",
        .layer_index = 1,
        .layer_count = 2,
        .heat = 0.9f,
    });

    IsometricCamera camera;
    camera.set_viewport(800, 600);
    camera.reframe_world_bounds(0.0f, 4.0f, 0.0f, 6.0f);

    MegaCityCodeConfig config;
    config.overlay_mode = OverlayMode::Perf;
    CodeVizSceneSnapshotResult result = build_scene_snapshot(camera, world, config, live_metrics, {}, nullptr, nullptr);

    REQUIRE(result.snapshot.camera.label_fade_px.z == Catch::Approx(1.0f));
    REQUIRE_FALSE(result.snapshot.objects.empty());
    REQUIRE(result.snapshot.performance_heat_values.size() == 2);
    CHECK(result.snapshot.performance_heat_values[0] == Catch::Approx(0.1f));
    CHECK(result.snapshot.performance_heat_values[1] == Catch::Approx(0.9f));
    CHECK(result.snapshot.objects[0].performance_heat_offset == 0u);
    CHECK(result.snapshot.objects[0].performance_heat_count == 2u);
}

TEST_CASE("megacity building roof sign expands for long text", "[megacity]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "src/example.cpp";
    file.symbols.push_back(SymbolRecord{
        SymbolKind::Class,
        "VeryLongBuildingName",
        "",
        false,
        1,
        24,
        1,
        {},
        {
            { "count_", "int", {} },
        },
    });
    snapshot.files.push_back(file);

    CodeVizSceneWorld world;
    MegaCityCodeConfig config;
    config.roof_sign_min_width_per_character = 0.6f;
    uint64_t sign_label_revision = 1;
    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);

    build_city(
        world,
        semantics,
        &text_service,
        config,
        sign_label_revision);

    constexpr std::string_view kBuildingName = "VeryLongBuildingName";
    constexpr size_t kMaxSignChars = 15;
    const size_t elided_len = std::min(kBuildingName.size(), kMaxSignChars);
    const float expected_min_sign_width = static_cast<float>(elided_len) * config.roof_sign_min_width_per_character
        + 2.0f * config.wall_sign_side_inset;

    bool found_building_sign = false;
    auto sign_view = world.registry().view<const SignMetrics, const CodeVizSemanticRef>();
    for (const entt::entity entity : sign_view)
    {
        const auto& metrics = sign_view.get<const SignMetrics>(entity);
        const auto& source = sign_view.get<const CodeVizSemanticRef>(entity);
        if (source.file != "src/example.cpp" || source.name != kBuildingName || source.module_path != "src")
            continue;
        found_building_sign = true;
        CHECK(metrics.width >= expected_min_sign_width - 1e-4f);
    }
    REQUIRE(found_building_sign);

    text_service.shutdown();
}

TEST_CASE("megacity scene snapshot carries custom building meshes", "[megacity]")
{
    CodeVizSceneWorld world;
    DraxulBuildingParams params;
    params.footprint = 4.0f;
    params.sides = 4;
    params.levels = {
        { 2.0f, glm::vec3(0.8f, 0.2f, 0.2f) },
        { 3.0f, glm::vec3(0.2f, 0.2f, 0.8f) },
    };
    auto custom_mesh = std::make_shared<GeometryMesh>(generate_draxul_building(params));

    const BuildingMetrics metrics{
        .footprint = 4.0f,
        .height = 5.0f,
        .sidewalk_width = 1.0f,
        .road_width = 2.0f,
    };
    world.create_building(
        1.0f,
        2.0f,
        0.25f,
        metrics,
        glm::vec4(1.0f),
        CodeVizSemanticRef{ "src/app.cpp", "App" },
        kCityWoodBuildingMaterial,
        custom_mesh);

    IsometricCamera camera;
    camera.look_at_world_center(1.0f, 2.0f);
    camera.set_viewport(800, 600);
    MegaCityCodeConfig config;

    const CodeVizSceneSnapshotResult result = build_scene_snapshot(
        camera,
        world,
        config,
        {},
        {},
        {},
        {});

    REQUIRE(result.snapshot.objects.size() == 1);
    REQUIRE(result.snapshot.custom_meshes.size() == 1);
    CHECK(result.snapshot.objects[0].mesh == CodeVizMeshId::Custom);
    CHECK(result.snapshot.objects[0].custom_mesh_index == 0);
    CHECK(result.snapshot.custom_meshes[0].get() == custom_mesh.get());
}

TEST_CASE("megacity scene snapshot carries custom sign meshes", "[megacity]")
{
    CodeVizSceneWorld world;
    DraxulRoofSignParams params;
    params.sides = 6;
    params.inner_radius = 1.8f;
    params.band_depth = 0.2f;
    params.height = 0.6f;
    auto custom_mesh = std::make_shared<GeometryMesh>(generate_draxul_roof_sign(params));

    const SignMetrics sign{
        .width = (params.inner_radius + params.band_depth) * 2.0f,
        .height = params.height,
        .depth = params.band_depth,
    };
    world.create_sign(
        0.0f,
        0.0f,
        3.0f,
        sign,
        CodeVizMeshId::Custom,
        glm::vec4(1.0f),
        CodeVizSemanticRef{ "src/app.cpp", "App" },
        custom_mesh);

    IsometricCamera camera;
    camera.look_at_world_center(0.0f, 0.0f);
    camera.set_viewport(800, 600);
    MegaCityCodeConfig config;

    const CodeVizSceneSnapshotResult result = build_scene_snapshot(
        camera,
        world,
        config,
        {},
        {},
        {},
        {});

    REQUIRE(result.snapshot.objects.size() == 1);
    REQUIRE(result.snapshot.custom_meshes.size() == 1);
    CHECK(result.snapshot.objects[0].mesh == CodeVizMeshId::Custom);
    CHECK(result.snapshot.objects[0].custom_mesh_index == 0);
    CHECK(result.snapshot.custom_meshes[0].get() == custom_mesh.get());
}

TEST_CASE("megacity picking distinguishes duplicate names in the same module by source file", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module;
    module.module_path = "tests";

    SemanticCityBuilding left;
    left.module_path = module.module_path;
    left.qualified_name = "FakeGlyphAtlas";
    left.display_name = "FakeGlyphAtlas";
    left.source_file_path = "tests/font_size_tests.cpp";
    left.metrics = BuildingMetrics{
        .footprint = 2.0f,
        .height = 4.0f,
        .sidewalk_width = 0.5f,
        .road_width = 1.0f,
    };
    left.center = glm::vec2(-4.0f, 0.0f);

    SemanticCityBuilding right = left;
    right.source_file_path = "tests/grid_rendering_pipeline_tests.cpp";
    right.center = glm::vec2(4.0f, 0.0f);

    module.buildings = { left, right };
    layout.modules.push_back(module);

    IsometricCamera camera;
    camera.set_viewport(800, 600);
    camera.frame_world_bounds(-8.0f, 8.0f, -4.0f, 4.0f);

    const CodeVizSceneSnapshot scene = snapshot_from_camera(camera);
    const glm::vec2 right_ndc = ndc_of_point(scene, glm::vec3(right.center.x, right.metrics.height * 0.5f, right.center.y));
    const glm::ivec2 screen_pos(
        static_cast<int>(std::lround((right_ndc.x * 0.5f + 0.5f) * 800.0f)),
        static_cast<int>(std::lround((1.0f - (right_ndc.y * 0.5f + 0.5f)) * 600.0f)));

    const auto picked = pick_building(screen_pos, 800, 600, camera, layout);
    REQUIRE(picked.has_value());
    CHECK(picked->qualified_name == "FakeGlyphAtlas");
    CHECK(picked->module_path == "tests");
    CHECK(picked->source_file_path == "tests/grid_rendering_pipeline_tests.cpp");
}

TEST_CASE("megacity picking still resolves the correct building in perspective mode", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module;
    module.module_path = "tests";

    SemanticCityBuilding left;
    left.module_path = module.module_path;
    left.qualified_name = "LeftBuilding";
    left.display_name = "LeftBuilding";
    left.source_file_path = "tests/left.cpp";
    left.metrics = BuildingMetrics{
        .footprint = 2.0f,
        .height = 4.0f,
        .sidewalk_width = 0.5f,
        .road_width = 1.0f,
    };
    left.center = glm::vec2(-4.0f, 0.0f);

    SemanticCityBuilding right = left;
    right.qualified_name = "RightBuilding";
    right.display_name = "RightBuilding";
    right.source_file_path = "tests/right.cpp";
    right.center = glm::vec2(4.0f, 0.0f);

    module.buildings = { left, right };
    layout.modules.push_back(module);

    IsometricCamera camera;
    camera.set_viewport(800, 600);
    camera.frame_world_bounds(-8.0f, 8.0f, -4.0f, 4.0f);
    camera.set_projection_mode(MegaCityProjectionMode::Perspective);

    const CodeVizSceneSnapshot scene = snapshot_from_camera(camera);
    const glm::vec2 right_ndc = ndc_of_point(scene, glm::vec3(right.center.x, right.metrics.height * 0.5f, right.center.y));
    const glm::ivec2 screen_pos(
        static_cast<int>(std::lround((right_ndc.x * 0.5f + 0.5f) * 800.0f)),
        static_cast<int>(std::lround((1.0f - (right_ndc.y * 0.5f + 0.5f)) * 600.0f)));

    const auto picked = pick_building(screen_pos, 800, 600, camera, layout);
    REQUIRE(picked.has_value());
    CHECK(picked->qualified_name == right.qualified_name);
    CHECK(picked->source_file_path == right.source_file_path);
}

TEST_CASE("megacity picking filter can skip disallowed duplicate-name buildings", "[megacity]")
{
    SemanticMegacityLayout layout;
    SemanticCityModuleLayout module;
    module.module_path = "tests";

    SemanticCityBuilding first;
    first.module_path = module.module_path;
    first.qualified_name = "FakeGlyphAtlas";
    first.display_name = "FakeGlyphAtlas";
    first.source_file_path = "tests/font_size_tests.cpp";
    first.metrics = BuildingMetrics{
        .footprint = 2.0f,
        .height = 4.0f,
        .sidewalk_width = 0.5f,
        .road_width = 1.0f,
    };
    first.center = glm::vec2(0.0f, 0.0f);

    SemanticCityBuilding second = first;
    second.source_file_path = "tests/grid_rendering_pipeline_tests.cpp";

    module.buildings = { first, second };
    layout.modules.push_back(module);

    IsometricCamera camera;
    camera.set_viewport(800, 600);
    camera.frame_world_bounds(-4.0f, 4.0f, -4.0f, 4.0f);

    const CodeVizSceneSnapshot scene = snapshot_from_camera(camera);
    const glm::vec2 ndc = ndc_of_point(scene, glm::vec3(0.0f, first.metrics.height * 0.5f, 0.0f));
    const glm::ivec2 screen_pos(
        static_cast<int>(std::lround((ndc.x * 0.5f + 0.5f) * 800.0f)),
        static_cast<int>(std::lround((1.0f - (ndc.y * 0.5f + 0.5f)) * 600.0f)));

    const auto picked = pick_building(
        screen_pos,
        800,
        600,
        camera,
        layout,
        [&](const std::string& source_file_path, const std::string&, const std::string&) {
            return source_file_path == second.source_file_path;
        });
    REQUIRE(picked.has_value());
    CHECK(picked->source_file_path == second.source_file_path);
}

TEST_CASE("procedural building side count becomes hex for heavily connected buildings", "[megacity]")
{
    CHECK(procedural_building_side_count(0, 12, 24) == 4);
    CHECK(procedural_building_side_count(11, 12, 24) == 4);
    CHECK(procedural_building_side_count(12, 12, 24) == 6);
    CHECK(procedural_building_side_count(23, 12, 24) == 6);
    CHECK(procedural_building_side_count(24, 12, 24) == 8);
    CHECK(procedural_building_side_count(9, 9, 18) == 6);
    CHECK(procedural_building_side_count(18, 9, 18) == 8);
    CHECK(procedural_building_side_count(12, 12, 8) == 6);
}

TEST_CASE("route segment world transform follows its intended direction", "[megacity]")
{
    CodeVizSceneWorld world;
    const glm::vec2 a{ 1.0f, 2.0f };
    const glm::vec2 b{ 4.0f, 5.0f };
    const glm::vec2 delta = glm::normalize(b - a);

    world.create_route_segment(
        (a.x + b.x) * 0.5f,
        (a.y + b.y) * 0.5f,
        RouteSegmentMetrics{
            .extent_x = glm::length(b - a),
            .extent_z = 0.1f,
            .height = 0.04f,
            .yaw_radians = -std::atan2(b.y - a.y, b.x - a.x),
        },
        glm::vec4(1.0f),
        {},
        0.0f);

    IsometricCamera camera;
    camera.look_at_world_center(2.5f, 3.5f);
    camera.set_viewport(800, 600);
    MegaCityCodeConfig config;

    const CodeVizSceneSnapshotResult result = build_scene_snapshot(
        camera,
        world,
        config,
        {},
        {},
        {},
        {});

    REQUIRE(result.snapshot.objects.size() == 1);
    const glm::mat4& world_matrix = result.snapshot.objects[0].world;
    const glm::vec3 local_start = glm::vec3(world_matrix * glm::vec4(-0.5f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 local_end = glm::vec3(world_matrix * glm::vec4(0.5f, 0.0f, 0.0f, 1.0f));
    const glm::vec2 world_dir = glm::normalize(glm::vec2(local_end.x - local_start.x, local_end.z - local_start.z));

    CHECK(world_dir.x == Catch::Approx(delta.x).margin(1e-4f));
    CHECK(world_dir.y == Catch::Approx(delta.y).margin(1e-4f));
}

#endif

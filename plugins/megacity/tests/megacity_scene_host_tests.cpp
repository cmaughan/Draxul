#include "support/megacity_scene_test_support.h"

#ifdef DRAXUL_ENABLE_MEGACITY

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

TEST_CASE("megacity camera perspective zoom dollies instead of changing orthographic span", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-30.0f, 30.0f, -20.0f, 20.0f);
    camera.set_projection_mode(MegaCityProjectionMode::Perspective);

    const IsometricCameraState before = camera.state();
    const float before_zoom = camera.zoom_half_height();
    camera.zoom_by(-0.5f);
    const IsometricCameraState after = camera.state();

    CHECK(after.orbit_radius < before.orbit_radius);
    CHECK(after.zoom_half_height == Catch::Approx(before.zoom_half_height));
    CHECK(camera.zoom_half_height() < before_zoom);
    CHECK(camera.proj_matrix()[3][3] == Catch::Approx(0.0f));
}

TEST_CASE("megacity camera pitch is clamped to a sensible range", "[megacity]")
{
    IsometricCamera camera;
    camera.frame_world_bounds(-10.0f, 10.0f, -10.0f, 10.0f);

    camera.adjust_pitch(10.0f);
    CHECK(camera.pitch_angle() == Catch::Approx(1.22173048f));

    camera.adjust_pitch(-10.0f);
    CHECK(camera.pitch_angle() == Catch::Approx(0.08726646f));
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
        .projection_mode = MegaCityProjectionMode::Perspective,
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
    CHECK(actual.projection_mode == desired.projection_mode);
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

TEST_CASE("megacity camera perspective screen drag moves content in the same direction", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_projection_mode(MegaCityProjectionMode::Perspective);
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
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    IFrameContext* frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    auto* pass = dynamic_cast<CodeVizScenePass*>(renderer.last_recorded_render_pass);
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
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    IFrameContext* frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    auto* pass = dynamic_cast<CodeVizScenePass*>(renderer.last_recorded_render_pass);
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

TEST_CASE("megacity host forwards text input into its ImGui context", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(host.imgui_context_ != nullptr);

    ImGui::SetCurrentContext(host.imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const int before_count = g.InputEventsQueue.Size;
    io.WantTextInput = true;
    callbacks.request_frame_calls = 0;

    host.on_text_input({ "abc" });

    REQUIRE(g.InputEventsQueue.Size >= before_count + 3);
    CHECK(g.InputEventsQueue[before_count + 0].Type == ImGuiInputEventType_Text);
    CHECK(g.InputEventsQueue[before_count + 0].Text.Char == unsigned('a'));
    CHECK(g.InputEventsQueue[before_count + 1].Type == ImGuiInputEventType_Text);
    CHECK(g.InputEventsQueue[before_count + 1].Text.Char == unsigned('b'));
    CHECK(g.InputEventsQueue[before_count + 2].Type == ImGuiInputEventType_Text);
    CHECK(g.InputEventsQueue[before_count + 2].Text.Char == unsigned('c'));
    CHECK(callbacks.request_frame_calls == 1);

    host.shutdown();
}

TEST_CASE("megacity host destroys scene pass before shutting down its imgui backend", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(host.scene_pass_ != nullptr);

    ShutdownOrderImGuiHost imgui_host;
    imgui_host.owner = &host;
    host.attach_imgui_host(imgui_host);

    host.shutdown();

    CHECK_FALSE(imgui_host.scene_pass_alive_during_shutdown);
}

TEST_CASE("megacity host source override controls the Tree-sitter scan root", "[megacity]")
{
    tests::TempDir temp("draxul-megacity-source-root");
    const auto scan_root = temp.path / "linux";
    std::filesystem::create_directories(scan_root);
    {
        std::ofstream out(scan_root / "sample.cpp", std::ios::trunc);
        out << "int sample_function() { return 7; }\n";
    }

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;
    launch.source_path = scan_root.string();

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.semantic_source_->root() == std::filesystem::weakly_canonical(scan_root));

    const auto snapshot = wait_for_complete_snapshot(*host.semantic_source_);
    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    CHECK(std::any_of(snapshot->files.begin(), snapshot->files.end(), [](const ParsedFile& file) {
        return file.path == "sample.cpp";
    }));

    host.shutdown();
}

TEST_CASE("megacity host publishes a code semantic snapshot without database state", "[megacity][treesitter]")
{
    tests::TempDir temp("draxul-megacity-treesitter-source-state");
    const auto scan_root = temp.path / "src";
    std::filesystem::create_directories(scan_root);
    {
        std::ofstream out(scan_root / "widget.cpp", std::ios::trunc);
        out << "class Widget { int count_; };\n";
    }

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;
    launch.source_path = temp.path.string();

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.semantic_source_->started());
    CHECK(host.code_semantics_ == nullptr);
    CHECK(host.semantic_source_->parsed_snapshot() == nullptr);
    CHECK_FALSE(host.semantic_source_->ready());

    const auto snapshot = wait_for_complete_snapshot(*host.semantic_source_);
    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);

    host.pump();

    CHECK(host.semantic_source_->ready());
    REQUIRE(host.code_semantics_ != nullptr);
    CHECK(host.code_semantics_->complete);
    CHECK(host.semantic_source_->parsed_snapshot() == snapshot);
    CHECK(host.semantic_source_->available_modules() == std::vector<std::string>{ "src" });
    REQUIRE(host.semantic_model_ != nullptr);
    REQUIRE(host.semantic_model_->modules.size() == 1);
    CHECK(host.semantic_model_->modules[0].module_path == "src");

    host.shutdown();
}

TEST_CASE("megacity host treats stale graphify config as Tree-sitter source", "[megacity][treesitter]")
{
    tests::TempDir temp("draxul-megacity-stale-graphify-config");
    const auto scan_root = temp.path / "src";
    std::filesystem::create_directories(scan_root);
    {
        std::ofstream out(scan_root / "widget.cpp", std::ios::trunc);
        out << "class Widget { int count_; };\n";
    }
    const auto missing_graph_path = temp.path / "missing-graph.json";

    ConfigDocument document;
    toml::table& code_table = document.ensure_table("mega_city_code");
    code_table.insert_or_assign("code_source", "graphify");
    code_table.insert_or_assign("graphify_graph_path", missing_graph_path.string());

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;
    launch.source_path = temp.path.string();

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .config_document = &document,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.semantic_source_->started());
    CHECK(host.code_semantics_ == nullptr);
    CHECK(host.semantic_source_->parsed_snapshot() == nullptr);
    CHECK_FALSE(host.semantic_source_->ready());

    const auto snapshot = wait_for_complete_snapshot(*host.semantic_source_);
    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);

    host.pump();

    CHECK(host.semantic_source_->ready());
    CHECK(host.code_semantics_ != nullptr);
    CHECK(host.code_semantics_->complete);
    CHECK(host.semantic_source_->parsed_snapshot() == snapshot);
    CHECK(host.semantic_source_->available_modules() == std::vector<std::string>{ "src" });
    REQUIRE(host.semantic_model_ != nullptr);
    REQUIRE(host.semantic_model_->modules.size() == 1);
    CHECK(host.semantic_model_->modules[0].module_path == "src");

    host.shutdown();
}

TEST_CASE("bioview host builds from neutral semantics without a city model", "[megacity][bioview][treesitter]")
{
    tests::TempDir temp("draxul-bioview-semantic-source-state");
    const auto scan_root = temp.path / "src";
    std::filesystem::create_directories(scan_root);
    {
        std::ofstream out(scan_root / "widget.cpp", std::ios::trunc);
        out << "struct WidgetConfig { int value; };\n"
            << "class Widget { WidgetConfig config_; void draw(); };\n"
            << "void Widget::draw() {}\n";
    }

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host(MegaCityVisualizationMode::Biology);

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;
    launch.source_path = temp.path.string();

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    const auto snapshot = wait_for_complete_snapshot(*host.semantic_source_);
    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);

    host.pump();

    CHECK(host.semantic_source_->ready());
    REQUIRE(host.code_semantics_ != nullptr);
    CHECK(host.code_semantics_->complete);
    CHECK(host.semantic_source_->available_modules() == std::vector<std::string>{ "src" });
    CHECK(host.semantic_model_ == nullptr);
    CHECK(host.semantic_layout_ == nullptr);
    CHECK(host.city_grid_ == nullptr);
    REQUIRE(host.world_ != nullptr);
    auto ellipsoid_view = host.world_->registry().view<const EllipsoidMetrics>();
    CHECK(ellipsoid_view.begin() != ellipsoid_view.end());

    host.shutdown();
}

TEST_CASE("megacity host retries focused routes once the grid becomes available", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));

    const auto fixture = make_roof_sign_semantic_fixture();

    host.semantic_model_ = fixture.model;
    host.semantic_layout_ = fixture.layout;
    host.selected_building_name_ = std::string(kRoofSignFunctionBundle);
    host.selected_building_module_path_ = std::string(kRoofSignModule);
    host.selected_building_source_file_.clear();
    host.selected_function_name_ = std::string(kRoofSignFunctionName);
    host.selection_routes_requested_ = false;
    host.city_grid_.reset();

    host.pump();
    CHECK_FALSE(host.selection_routes_requested_);

    CityGrid grid = build_city_grid(*fixture.layout, *fixture.model, host.renderer_config_);
    REQUIRE(grid.routes.empty());
    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        host.city_grid_ = std::make_shared<CityGrid>(std::move(grid));
    }

    host.pump();
    CHECK(host.selection_routes_requested_);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        host.pump();
        std::shared_ptr<const CityGrid> routed_grid;
        {
            std::lock_guard<std::mutex> lock(host.grid_mutex_);
            routed_grid = host.city_grid_;
        }
        if (routed_grid && !routed_grid->routes.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::shared_ptr<const CityGrid> routed_grid;
    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        routed_grid = host.city_grid_;
    }
    REQUIRE(routed_grid != nullptr);
    REQUIRE(routed_grid->routes.size() == 1);
    CHECK(routed_grid->routes[0].source_qualified_name == kRoofSignFunctionName);
    CHECK(routed_grid->routes[0].target_qualified_name == kRoofSignStructName);

    host.shutdown();
}

TEST_CASE("megacity grid rebuild request does not join an in-flight worker", "[megacity]")
{
    MegaCityHost host;
    std::atomic<bool> release_worker{ false };
    host.grid_build_in_progress_ = true;
    host.grid_thread_ = std::thread([&release_worker]() {
        while (!release_worker.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });

    SemanticMegacityLayout empty_layout;
    SemanticMegacityModel empty_model;
    auto rebuild = std::async(std::launch::async, [&host, &empty_layout, &empty_model]() {
        host.launch_grid_build(empty_layout, empty_model);
    });

    const auto status = rebuild.wait_for(std::chrono::milliseconds(50));
    release_worker = true;
    REQUIRE(rebuild.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    rebuild.get();
    host.shutdown();

    CHECK(status == std::future_status::ready);
}

TEST_CASE("megacity host scene click on roof sign function emits focused dependency route", "[megacity][integration]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));

    const auto fixture = make_roof_sign_semantic_fixture();

    host.semantic_model_ = fixture.model;
    host.semantic_layout_ = fixture.layout;
    host.city_bounds_valid_ = true;
    host.city_min_x_ = fixture.layout->min_x;
    host.city_max_x_ = fixture.layout->max_x;
    host.city_min_z_ = fixture.layout->min_z;
    host.city_max_z_ = fixture.layout->max_z;
    host.sign_label_revision_ = 1;
    host.camera_->frame_world_bounds(
        fixture.layout->min_x,
        fixture.layout->max_x,
        fixture.layout->min_z,
        fixture.layout->max_z);
    host.scene_dirty_ = true;

    CityGrid grid = build_city_grid(*fixture.layout, *fixture.model, host.renderer_config_);
    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        host.city_grid_ = std::make_shared<CityGrid>(std::move(grid));
    }

    const SemanticCityBuilding* function_bundle = nullptr;
    const SemanticBuildingLayer* roof_sign_layer = nullptr;
    for (const auto& module : host.semantic_layout_->modules)
    {
        for (const auto& building : module.buildings)
        {
            if (!building.is_free_function)
                continue;
            for (const auto& layer : building.layers)
            {
                if (layer.function_name == kRoofSignFunctionName)
                {
                    function_bundle = &building;
                    roof_sign_layer = &layer;
                    break;
                }
            }
            if (roof_sign_layer)
                break;
        }
        if (roof_sign_layer)
            break;
    }

    REQUIRE(function_bundle != nullptr);
    REQUIRE(roof_sign_layer != nullptr);

    float cumulative_height = 0.0f;
    float layer_mid_y = building_base_elevation(host.renderer_config_);
    for (const auto& layer : function_bundle->layers)
    {
        cumulative_height += layer.height;
        if (layer.function_name == roof_sign_layer->function_name)
        {
            layer_mid_y = building_base_elevation(host.renderer_config_) + cumulative_height - layer.height * 0.5f;
            break;
        }
    }

    host.pump();
    IFrameContext* frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    auto* pass = dynamic_cast<CodeVizScenePass*>(renderer.last_recorded_render_pass);
    REQUIRE(pass != nullptr);

    const glm::vec2 ndc = ndc_of_point(
        pass->scene(),
        glm::vec3(function_bundle->center.x, layer_mid_y, function_bundle->center.y));
    const glm::ivec2 screen_pos(
        static_cast<int>(std::lround((ndc.x * 0.5f + 0.5f) * 800.0f)),
        static_cast<int>(std::lround((1.0f - (ndc.y * 0.5f + 0.5f)) * 600.0f)));

    std::optional<glm::ivec2> precise_screen_pos;
    for (int dy = -12; dy <= 12 && !precise_screen_pos.has_value(); ++dy)
    {
        for (int dx = -12; dx <= 12; ++dx)
        {
            const glm::ivec2 candidate = screen_pos + glm::ivec2(dx, dy);
            auto hit = pick_building(
                candidate,
                800,
                600,
                *host.camera_,
                *host.semantic_layout_,
                {},
                host.semantic_model_.get(),
                &host.renderer_config_);
            if (!hit)
                continue;
            if (hit->qualified_name != kRoofSignFunctionBundle || !hit->has_layer_index)
                continue;
            precise_screen_pos = candidate;
            break;
        }
    }

    REQUIRE(precise_screen_pos.has_value());

    click_megacity_scene(host, *precise_screen_pos);

    CHECK(host.selected_building_name_ == function_bundle->qualified_name);
    CHECK(host.selected_building_module_path_ == function_bundle->module_path);
    CHECK(host.selected_function_name_ == kRoofSignFunctionName);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        host.pump();
        std::shared_ptr<const CityGrid> routed_grid;
        {
            std::lock_guard<std::mutex> lock(host.grid_mutex_);
            routed_grid = host.city_grid_;
        }
        if (routed_grid && !routed_grid->routes.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::shared_ptr<const CityGrid> routed_grid;
    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        routed_grid = host.city_grid_;
    }
    REQUIRE(routed_grid != nullptr);
    REQUIRE_FALSE(routed_grid->routes.empty());
    CHECK(std::any_of(
        routed_grid->routes.begin(),
        routed_grid->routes.end(),
        [](const CityGrid::RoutePolyline& route) {
            return route.source_qualified_name == kRoofSignFunctionName
                && route.target_qualified_name == kRoofSignStructName;
        }));

    host.pump();
    frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    pass = dynamic_cast<CodeVizScenePass*>(renderer.last_recorded_render_pass);
    REQUIRE(pass != nullptr);

    bool saw_visible_route_segment = false;
    for (const auto& obj : pass->scene().objects)
    {
        if (obj.route_source == kRoofSignFunctionName
            && obj.route_target == kRoofSignStructName)
        {
            saw_visible_route_segment = true;
            CHECK(obj.color.a == Catch::Approx(1.0f));
        }
    }
    CHECK(saw_visible_route_segment);

    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        auto cleared_grid = std::make_shared<CityGrid>(*host.city_grid_);
        cleared_grid->routes.clear();
        host.city_grid_ = std::move(cleared_grid);
    }
    host.world_->clear_route_segments();
    host.selection_routes_requested_ = false;

    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    click_megacity_scene(host, *precise_screen_pos);

    const auto retry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < retry_deadline)
    {
        host.pump();
        std::shared_ptr<const CityGrid> retried_grid;
        {
            std::lock_guard<std::mutex> lock(host.grid_mutex_);
            retried_grid = host.city_grid_;
        }
        if (retried_grid && !retried_grid->routes.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        std::lock_guard<std::mutex> lock(host.grid_mutex_);
        REQUIRE(host.city_grid_ != nullptr);
        REQUIRE_FALSE(host.city_grid_->routes.empty());
    }

    host.shutdown();
}

TEST_CASE("megacity host preserves externally edited core config when saving megacity settings", "[megacity][config]")
{
    tests::TempDir temp("draxul-megacity-config-merge");
    tests::HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "palette_bg_alpha = 0.9\n"
               "[mega_city_code]\n"
               "show_ui_panels = true\n";
    }

    ConfigDocument document = ConfigDocument::load();

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .config_document = &document,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));

    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "palette_bg_alpha = 0.6\n"
               "[mega_city_code]\n"
               "show_ui_panels = false\n";
    }

    host.shutdown();

    const std::string saved = read_text_file(redir.config_path);
    // TOML serializes 0.6 with full double precision (0.59999999999999998).
    // Verify the value was preserved from the externally-edited file (not
    // reverted to the original 0.9) by parsing the saved config.
    AppConfig reloaded = AppConfig::parse(saved);
    REQUIRE(reloaded.palette_bg_alpha == Catch::Approx(0.6f).margin(0.001f));
}

TEST_CASE("megacity host keeps catching up between mouse samples", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    IFrameContext* frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    auto* pass = dynamic_cast<CodeVizScenePass*>(renderer.last_recorded_render_pass);
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

TEST_CASE("megacity host draw does not schedule a follow-up frame for selection opacity", "[megacity]")
{
    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::Plugin;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));

    callbacks.request_frame_calls = 0;
    host.semantic_model_ = std::make_shared<SemanticMegacityModel>();
    host.selected_building_name_ = "Selected";
    host.selected_building_module_path_ = "libs/example";
    host.selected_building_source_file_ = "src/example.cpp";
    host.scene_dirty_ = true;

    IFrameContext* frame = renderer.begin_frame();
    REQUIRE(frame != nullptr);
    host.draw(*frame);
    renderer.end_frame();

    CHECK(callbacks.request_frame_calls == 0);

    host.shutdown();
}

#endif

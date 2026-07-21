#pragma once

#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "biology_builder.h"
#include "city_builder.h"
#include "city_helpers.h"
#include "city_materials.h"
#include "city_meshes.h"
#include "city_picking.h"
#include "fake_renderer.h"
#include "fake_window.h"
#include "home_dir_redirect.h"
#include "live_city_metrics.h"
#include "mesh_library.h"
#include "scene_snapshot_builder.h"
#include "semantic_source_controller.h"
#include "semantic_city_layout.h"
#include "temp_dir.h"
#include "test_host_callbacks.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/building_generator.h>
#include <draxul/codeviz_scene_pass.h>
#include <draxul/codeviz_scene_world.h>
#include <draxul/config_document.h>
#include <draxul/imgui_host.h>
#include <draxul/isometric_camera.h>
#define private public
#include <draxul/megacity_host.h>
#undef private
#include <draxul/roof_sign_generator.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <imgui_internal.h>
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

glm::vec2 ndc_of_point(const CodeVizSceneSnapshot& scene, const glm::vec3& point)
{
    const glm::vec4 clip = scene.camera.proj * scene.camera.view * glm::vec4(point, 1.0f);
    return glm::vec2(clip) / clip.w;
}

CodeVizSceneSnapshot snapshot_from_camera(const IsometricCamera& camera)
{
    CodeVizSceneSnapshot scene;
    scene.camera.view = camera.view_matrix();
    scene.camera.proj = camera.proj_matrix();
    return scene;
}

void click_megacity_scene(MegaCityHost& host, const glm::ivec2& screen_pos)
{
    host.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, screen_pos });
    host.on_mouse_button({ SDL_BUTTON_LEFT, false, kModNone, screen_pos });
    host.pump();
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

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::shared_ptr<const CodebaseSnapshot> wait_for_complete_snapshot(
    CodebaseScanner& scanner,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto snapshot = scanner.snapshot(); snapshot && snapshot->complete)
            return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return scanner.snapshot();
}

std::shared_ptr<const CodebaseSnapshot> wait_for_complete_snapshot(
    SemanticSourceController& source,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto snapshot = source.scanner_snapshot(); snapshot && snapshot->complete)
            return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return source.scanner_snapshot();
}

struct ShutdownOrderImGuiHost : IImGuiHost
{
    MegaCityHost* owner = nullptr;
    bool scene_pass_alive_during_shutdown = false;

    bool initialize_imgui_backend() override
    {
        return true;
    }

    void shutdown_imgui_backend() override
    {
        scene_pass_alive_during_shutdown = owner && static_cast<bool>(owner->scene_pass_);
    }

    void rebuild_imgui_font_texture() override {}
    void begin_imgui_frame() override {}
};

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

constexpr std::string_view kRoofSignModule = "libs/draxul-geometry";
constexpr std::string_view kRoofSignFunctionBundle = "Functions";
constexpr std::string_view kRoofSignFunctionName = "generate_draxul_roof_sign";
constexpr std::string_view kRoofSignFunctionFile = "libs/draxul-geometry/src/roof_sign_generator.cpp";
constexpr std::string_view kRoofSignStructName = "DraxulRoofSignParams";
constexpr std::string_view kRoofSignStructFile = "libs/draxul-geometry/include/draxul/roof_sign_generator.h";

struct RoofSignSemanticFixture
{
    std::shared_ptr<SemanticMegacityModel> model;
    std::shared_ptr<SemanticMegacityLayout> layout;
};

RoofSignSemanticFixture make_roof_sign_semantic_fixture()
{
    SemanticCityBuilding function_bundle;
    function_bundle.module_path = std::string(kRoofSignModule);
    function_bundle.display_name = std::string(kRoofSignFunctionBundle);
    function_bundle.qualified_name = std::string(kRoofSignFunctionBundle);
    function_bundle.is_free_function = true;
    function_bundle.function_count = 1;
    function_bundle.metrics.footprint = 2.0f;
    function_bundle.metrics.height = 1.2f;
    function_bundle.center = { -3.0f, 0.0f };
    function_bundle.layers.push_back({
        std::string(kRoofSignFunctionName),
        std::string(kRoofSignFunctionFile),
        3,
        function_bundle.metrics.height,
    });

    SemanticCityBuilding params_struct;
    params_struct.module_path = std::string(kRoofSignModule);
    params_struct.display_name = std::string(kRoofSignStructName);
    params_struct.qualified_name = std::string(kRoofSignStructName);
    params_struct.source_file_path = std::string(kRoofSignStructFile);
    params_struct.is_struct = true;
    params_struct.base_size = 5;
    params_struct.metrics.footprint = 2.0f;
    params_struct.metrics.height = 1.0f;
    params_struct.center = { 3.0f, 0.0f };

    SemanticCityModuleModel module_model;
    module_model.module_path = std::string(kRoofSignModule);
    module_model.buildings = { function_bundle, params_struct };
    module_model.function_bundle_remap.emplace(
        std::string(kRoofSignFunctionName),
        std::string(kRoofSignFunctionBundle));

    auto semantic_model = std::make_shared<SemanticMegacityModel>();
    semantic_model->modules.push_back(module_model);
    semantic_model->function_bundle_remap.emplace(
        std::string(kRoofSignFunctionName),
        std::string(kRoofSignFunctionBundle));
    semantic_model->dependencies.push_back({
        std::string(kRoofSignModule),
        std::string(kRoofSignFunctionName),
        "& input_params",
        std::string(kRoofSignStructName),
        std::string(kRoofSignModule),
        std::string(kRoofSignStructName),
        std::string(kRoofSignFunctionFile),
        std::string(kRoofSignStructFile),
        false,
    });

    auto semantic_layout = std::make_shared<SemanticMegacityLayout>();
    SemanticCityModuleLayout module_layout;
    module_layout.module_path = std::string(kRoofSignModule);
    module_layout.buildings = { function_bundle, params_struct };
    module_layout.min_x = -5.0f;
    module_layout.max_x = 5.0f;
    module_layout.min_z = -2.0f;
    module_layout.max_z = 2.0f;
    semantic_layout->modules.push_back(module_layout);
    semantic_layout->min_x = module_layout.min_x;
    semantic_layout->max_x = module_layout.max_x;
    semantic_layout->min_z = module_layout.min_z;
    semantic_layout->max_z = module_layout.max_z;

    return {
        .model = std::move(semantic_model),
        .layout = std::move(semantic_layout),
    };
}

std::filesystem::path bundled_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

bool init_text_service(TextService& text_service)
{
    const std::filesystem::path font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        return false;

    TextServiceConfig config;
    config.font_path = font_path.string();
    return text_service.initialize(config, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

} // namespace

#endif

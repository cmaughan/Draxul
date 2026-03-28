#include "scene_snapshot_builder.h"
#include "city_helpers.h"
#include "isometric_camera.h"
#include "scene_world.h"
#include "sign_label_atlas.h"
#include <algorithm>
#include <cmath>
#include <draxul/megacity_code_config.h>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace draxul
{

namespace
{

SceneMaterial build_scene_material(const Appearance& appearance)
{
    SceneMaterial material;
    switch (appearance.material)
    {
    case MaterialId::AsphaltRoad:
        material.shading_model = MaterialShadingModel::TexturedTintedPbr;
        material.scalar_params = glm::vec4(
            appearance.material_info.y,
            appearance.material_info.z,
            appearance.material_info.w,
            0.0f);
        material.texture_indices = glm::uvec4(
            static_cast<uint32_t>(SceneTextureId::AsphaltAlbedo),
            static_cast<uint32_t>(SceneTextureId::AsphaltNormal),
            static_cast<uint32_t>(SceneTextureId::AsphaltRoughness),
            static_cast<uint32_t>(SceneTextureId::AsphaltAo));
        break;
    case MaterialId::PavingSidewalk:
        material.shading_model = MaterialShadingModel::TexturedTintedPbr;
        material.scalar_params = glm::vec4(
            appearance.material_info.y,
            appearance.material_info.z,
            appearance.material_info.w,
            0.0f);
        material.texture_indices = glm::uvec4(
            static_cast<uint32_t>(SceneTextureId::SidewalkAlbedo),
            static_cast<uint32_t>(SceneTextureId::SidewalkNormal),
            static_cast<uint32_t>(SceneTextureId::SidewalkRoughness),
            static_cast<uint32_t>(SceneTextureId::SidewalkAo));
        break;
    case MaterialId::WoodBuilding:
        material.shading_model = MaterialShadingModel::VertexTintPbr;
        material.scalar_params = glm::vec4(
            appearance.material_info.y,
            appearance.material_info.z,
            appearance.material_info.w,
            0.0f);
        material.texture_indices = glm::uvec4(
            static_cast<uint32_t>(SceneTextureId::WoodAlbedo),
            static_cast<uint32_t>(SceneTextureId::WoodNormal),
            static_cast<uint32_t>(SceneTextureId::WoodRoughness),
            static_cast<uint32_t>(SceneTextureId::WoodAo));
        break;
    case MaterialId::LeafCards:
        material.shading_model = MaterialShadingModel::LeafCutoutPbr;
        material.scalar_params = glm::vec4(
            appearance.material_info.y,
            appearance.material_info.z,
            appearance.material_info.w,
            0.0f);
        material.texture_indices = glm::uvec4(
            static_cast<uint32_t>(SceneTextureId::LeafAlbedo),
            static_cast<uint32_t>(SceneTextureId::LeafNormal),
            static_cast<uint32_t>(SceneTextureId::LeafRoughness),
            static_cast<uint32_t>(SceneTextureId::LeafOpacity));
        material.metadata = glm::uvec4(
            0u,
            static_cast<uint32_t>(SceneTextureId::LeafScattering),
            0u,
            0u);
        break;
    case MaterialId::TreeBark:
        material.shading_model = MaterialShadingModel::TexturedTintedPbr;
        material.scalar_params = glm::vec4(
            appearance.material_info.y,
            appearance.material_info.z,
            appearance.material_info.w,
            0.0f);
        material.texture_indices = glm::uvec4(
            static_cast<uint32_t>(SceneTextureId::BarkAlbedo),
            static_cast<uint32_t>(SceneTextureId::BarkNormal),
            static_cast<uint32_t>(SceneTextureId::BarkRoughness),
            static_cast<uint32_t>(SceneTextureId::BarkAo));
        break;
    case MaterialId::FlatColor:
    default:
        break;
    }
    return material;
}

bool same_scene_material(const SceneMaterial& lhs, const SceneMaterial& rhs)
{
    return lhs.shading_model == rhs.shading_model
        && lhs.scalar_params == rhs.scalar_params
        && lhs.texture_indices == rhs.texture_indices
        && lhs.metadata == rhs.metadata;
}

uint32_t find_or_append_material(SceneSnapshot& scene, const Appearance& appearance)
{
    const SceneMaterial candidate = build_scene_material(appearance);
    for (uint32_t index = 0; index < scene.materials.size(); ++index)
    {
        if (same_scene_material(scene.materials[index], candidate))
            return index;
    }

    if (scene.materials.size() >= kMaxSceneMaterials)
        return 0;

    scene.materials.push_back(candidate);
    return static_cast<uint32_t>(scene.materials.size() - 1);
}

uint32_t find_or_append_custom_mesh(SceneSnapshot& scene, const std::shared_ptr<const MeshData>& mesh)
{
    for (uint32_t index = 0; index < scene.custom_meshes.size(); ++index)
    {
        if (scene.custom_meshes[index].get() == mesh.get())
            return index;
    }

    scene.custom_meshes.push_back(mesh);
    return static_cast<uint32_t>(scene.custom_meshes.size() - 1);
}

} // namespace

SceneSnapshotResult build_scene_snapshot(
    const IsometricCamera& camera,
    const SceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<SignLabelAtlas>& label_atlas,
    const std::shared_ptr<const MeshData>& tree_bark_mesh,
    const std::shared_ptr<const MeshData>& tree_leaf_mesh)
{
    SceneSnapshotResult result;
    SceneSnapshot& scene = result.snapshot;
    scene.tree_bark_mesh = tree_bark_mesh;
    scene.tree_leaf_mesh = tree_leaf_mesh;

    scene.camera.view = camera.view_matrix();
    scene.camera.proj = camera.proj_matrix();
    scene.camera.inv_view_proj = glm::inverse(scene.camera.proj * scene.camera.view);
    scene.camera.camera_pos = glm::vec4(camera.position(), 1.0f);
    scene.camera.light_dir = glm::normalize(glm::vec4(config.directional_light_dir, 0.0f));
    scene.camera.label_fade_px = glm::vec4(
        config.sign_text_px_range.x,
        config.sign_text_px_range.y,
        0.0f,
        0.0f);
    scene.camera.render_tuning = glm::vec4(
        config.output_gamma,
        config.point_light_brightness,
        config.ambient_strength,
        0.0f);
    scene.camera.ao_settings = glm::vec4(
        config.ao_radius,
        config.ao_bias,
        config.ao_power,
        0.0f);
    scene.camera.debug_view = glm::vec4(
        static_cast<float>(config.debug_view),
        config.ao_denoise ? 1.0f : 0.0f,
        static_cast<float>(config.ao_kernel_size),
        config.wireframe ? 1.0f : 0.0f);

    const GroundFootprint footprint = camera.visible_ground_footprint(0.0f);
    const float tile_size = world.tile_size();
    const float grid_tile_size = tile_size * config.world_floor_grid_tile_scale;
    scene.floor_grid.enabled = true;
    scene.floor_grid.min_x = static_cast<int>(std::floor(footprint.min_x / grid_tile_size)) - 1;
    scene.floor_grid.max_x = static_cast<int>(std::ceil(footprint.max_x / grid_tile_size)) + 1;
    scene.floor_grid.min_z = static_cast<int>(std::floor(footprint.min_z / grid_tile_size)) - 1;
    scene.floor_grid.max_z = static_cast<int>(std::ceil(footprint.max_z / grid_tile_size)) + 1;
    scene.floor_grid.tile_size = grid_tile_size;
    scene.floor_grid.line_width = tile_size * config.world_floor_grid_line_width;
    scene.floor_grid.y = config.world_floor_top_y
        - world_floor_height(config)
        - config.world_floor_grid_y_offset;
    scene.floor_grid.color = glm::vec4(0.62f, 0.62f, 0.66f, 1.0f);
    if (label_atlas)
    {
        std::shared_ptr<const SignLabelAtlas> alias(label_atlas);
        scene.label_atlas = std::shared_ptr<const LabelAtlasData>(alias, &label_atlas->image);
    }

    scene.materials.clear();
    scene.materials.push_back(SceneMaterial{});
    scene.custom_meshes.clear();

    // Query the ECS registry for all entities with position + appearance.
    const auto& reg = world.registry();
    auto view = reg.view<const WorldPosition, const Elevation, const Appearance>();
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    float building_min_x = std::numeric_limits<float>::max();
    float building_max_x = std::numeric_limits<float>::lowest();
    float building_min_z = std::numeric_limits<float>::max();
    float building_max_z = std::numeric_limits<float>::lowest();
    float max_building_lot_margin = 0.0f;
    for (auto [entity, pos, elev, appearance] : view.each())
    {
        SceneObject obj;
        obj.mesh = appearance.mesh;
        obj.material_index = find_or_append_material(scene, appearance);
        obj.double_sided = appearance.double_sided;
        const glm::vec3 world_pos{ pos.x, elev.value, pos.z };
        float extent_x = 1.0f;
        float extent_z = 1.0f;

        // Scale the cube by building metrics if present.
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), world_pos);
        const auto* custom_mesh = reg.try_get<CustomMeshRef>(entity);
        if (custom_mesh && custom_mesh->mesh)
        {
            obj.mesh = MeshId::Custom;
            obj.custom_mesh_index = find_or_append_custom_mesh(scene, custom_mesh->mesh);
        }

        if (const auto* bm = reg.try_get<BuildingMetrics>(entity))
        {
            extent_x = bm->footprint;
            extent_z = bm->footprint;
            if (obj.mesh != MeshId::Custom)
            {
                transform = glm::translate(transform, glm::vec3(0.0f, bm->height * 0.5f, 0.0f));
                transform = glm::scale(transform, glm::vec3(bm->footprint, bm->height, bm->footprint));
            }
            building_min_x = std::min(building_min_x, pos.x - bm->footprint * 0.5f);
            building_max_x = std::max(building_max_x, pos.x + bm->footprint * 0.5f);
            building_min_z = std::min(building_min_z, pos.z - bm->footprint * 0.5f);
            building_max_z = std::max(building_max_z, pos.z + bm->footprint * 0.5f);
            max_building_lot_margin = std::max(
                max_building_lot_margin, bm->sidewalk_width + bm->road_width);
        }
        else if (const auto* tm = reg.try_get<TreeMetrics>(entity))
        {
            extent_x = tm->canopy_radius * 2.0f;
            extent_z = tm->canopy_radius * 2.0f;
        }
        else if (const auto* rm = reg.try_get<RoadMetrics>(entity))
        {
            extent_x = rm->extent_x;
            extent_z = rm->extent_z;
            obj.uv_rect = glm::vec4(0.0f, 0.0f, rm->extent_x, rm->extent_z);
            transform = glm::translate(transform, glm::vec3(0.0f, rm->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(rm->extent_x, rm->height, rm->extent_z));
        }
        else if (const auto* rsm = reg.try_get<RoadSurfaceMetrics>(entity))
        {
            extent_x = rsm->extent_x;
            extent_z = rsm->extent_z;
            obj.uv_rect = glm::vec4(0.0f, 0.0f, rsm->extent_x, rsm->extent_z);
            transform = glm::translate(transform, glm::vec3(0.0f, rsm->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(rsm->extent_x, rsm->height, rsm->extent_z));
        }
        else if (const auto* route = reg.try_get<RouteSegmentMetrics>(entity))
        {
            extent_x = route->extent_x;
            extent_z = route->extent_z;
            transform = glm::translate(transform, glm::vec3(0.0f, route->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(route->extent_x, route->height, route->extent_z));
        }
        else if (const auto* module_surface = reg.try_get<ModuleSurfaceMetrics>(entity))
        {
            extent_x = module_surface->extent_x;
            extent_z = module_surface->extent_z;
            transform = glm::translate(transform, glm::vec3(0.0f, module_surface->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(module_surface->extent_x, module_surface->height, module_surface->extent_z));
        }
        else if (const auto* sm = reg.try_get<SignMetrics>(entity))
        {
            const bool quarter_turn = std::abs(std::sin(sm->yaw_radians)) > 0.70710678f;
            extent_x = quarter_turn ? sm->depth : sm->width;
            extent_z = quarter_turn ? sm->width : sm->depth;
            transform = glm::rotate(transform, sm->yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(sm->width, sm->height, sm->depth));
            obj.uv_rect = sm->uv_rect;
            obj.label_ink_pixel_size = sm->label_ink_pixel_size;
        }
        else
        {
            transform = glm::translate(transform, glm::vec3(0.0f, 0.5f, 0.0f));
        }

        obj.world = transform;
        obj.color = appearance.color;

        if (const auto* sym = reg.try_get<SourceSymbol>(entity))
            obj.source_name = sym->name;
        if (const auto* link = reg.try_get<RouteLink>(entity))
        {
            obj.route_source = link->source_qualified_name;
            obj.route_target = link->target_qualified_name;
        }

        scene.objects.push_back(std::move(obj));

        min_x = std::min(min_x, pos.x - extent_x * 0.5f);
        max_x = std::max(max_x, pos.x + extent_x * 0.5f);
        min_z = std::min(min_z, pos.z - extent_z * 0.5f);
        max_z = std::max(max_z, pos.z + extent_z * 0.5f);
    }

    if (min_x > max_x || min_z > max_z)
    {
        min_x = -2.5f;
        max_x = 2.5f;
        min_z = -2.5f;
        max_z = 2.5f;
    }

    const float span = std::max(max_x - min_x, max_z - min_z);
    result.world_span = std::max(span, 1.0f);
    scene.camera.world_debug_bounds = glm::vec4(min_x, max_x, min_z, max_z);
    scene.camera.point_light_pos = glm::vec4(
        config.point_light_position,
        std::max(config.point_light_radius, 1.0f));

    sort_scene_objects(scene);

    return result;
}

void sort_scene_objects(SceneSnapshot& scene)
{
    const glm::vec3 cam_pos(scene.camera.camera_pos);

    // Partition: opaque objects first, then transparent.
    auto partition_it = std::stable_partition(
        scene.objects.begin(), scene.objects.end(),
        [](const SceneObject& obj) { return obj.color.a >= 1.0f; });

    scene.opaque_count = static_cast<uint32_t>(std::distance(scene.objects.begin(), partition_it));

    // Sort transparent objects back-to-front by distance from camera.
    std::sort(partition_it, scene.objects.end(),
        [&cam_pos](const SceneObject& a, const SceneObject& b) {
            const glm::vec3 ca(a.world[3]);
            const glm::vec3 cb(b.world[3]);
            return glm::dot(ca - cam_pos, ca - cam_pos) > glm::dot(cb - cam_pos, cb - cam_pos);
        });
}

} // namespace draxul

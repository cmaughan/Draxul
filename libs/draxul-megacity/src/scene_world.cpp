#include "scene_world.h"

#include <draxul/perf_timing.h>

namespace draxul
{

namespace
{

constexpr float kWoodBuildingUvScale = 0.45f;
constexpr float kWoodBuildingNormalStrength = 0.7f;
constexpr float kWoodBuildingAoStrength = 0.45f;
constexpr float kTreeBarkUvScale = 1.0f;
constexpr float kTreeBarkNormalStrength = 0.6f;
constexpr float kTreeBarkAoStrength = 0.28f;
constexpr float kSidewalkPavingUvScale = 0.10625f;
constexpr float kSidewalkPavingNormalStrength = 0.8f;
constexpr float kSidewalkPavingAoStrength = 0.55f;
constexpr float kLeafAtlasUvScale = 1.0f;
constexpr float kLeafAtlasNormalStrength = 0.55f;
constexpr float kLeafAtlasScatteringStrength = 0.85f;

} // namespace

SceneWorld::SceneWorld() = default;

void SceneWorld::clear()
{
    registry_.clear();
}

void SceneWorld::clear_route_segments()
{
    PERF_MEASURE();
    std::vector<entt::entity> entities;
    auto view = registry_.view<RouteSegmentMetrics>();
    for (const entt::entity entity : view)
        entities.push_back(entity);
    for (const entt::entity entity : entities)
        registry_.destroy(entity);
}

entt::entity SceneWorld::create_building(float world_x, float world_z, float elevation,
    const BuildingMetrics& metrics, const glm::vec4& color, SourceSymbol source,
    MaterialId material, std::shared_ptr<const GeometryMesh> custom_mesh, float flat_metallic)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<BuildingMetrics>(entity, metrics);
    const MeshId mesh_id = custom_mesh ? MeshId::Custom : MeshId::Cube;
    if (material == MaterialId::WoodBuilding)
    {
        registry_.emplace<Appearance>(
            entity,
            mesh_id,
            MaterialId::WoodBuilding,
            false,
            color,
            glm::vec4(
                static_cast<float>(MaterialId::WoodBuilding),
                kWoodBuildingUvScale,
                kWoodBuildingNormalStrength,
                kWoodBuildingAoStrength));
    }
    else if (material == MaterialId::PavingSidewalk)
    {
        registry_.emplace<Appearance>(
            entity,
            mesh_id,
            MaterialId::PavingSidewalk,
            false,
            color,
            glm::vec4(
                static_cast<float>(MaterialId::PavingSidewalk),
                kSidewalkPavingUvScale,
                kSidewalkPavingNormalStrength,
                kSidewalkPavingAoStrength));
    }
    else
    {
        registry_.emplace<Appearance>(
            entity, mesh_id, material, false, color, glm::vec4(flat_metallic, 1.0f, 1.0f, 1.0f));
    }
    if (custom_mesh)
        registry_.emplace<CustomMeshRef>(entity, std::move(custom_mesh));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_tree_bark(float world_x, float world_z, float elevation,
    const TreeMetrics& metrics, const glm::vec4& color, SourceSymbol source)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<TreeMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::TreeBark,
        MaterialId::TreeBark,
        false,
        color,
        glm::vec4(
            static_cast<float>(MaterialId::TreeBark),
            kTreeBarkUvScale,
            kTreeBarkNormalStrength,
            kTreeBarkAoStrength));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_tree_leaves(float world_x, float world_z, float elevation,
    const TreeMetrics& metrics, const glm::vec4& color, SourceSymbol source)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<TreeMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::TreeLeaves,
        MaterialId::LeafCards,
        false,
        color,
        glm::vec4(
            static_cast<float>(MaterialId::LeafCards),
            kLeafAtlasUvScale,
            kLeafAtlasNormalStrength,
            kLeafAtlasScatteringStrength));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_road(float world_x, float world_z,
    const RoadMetrics& metrics, const glm::vec4& color, SourceSymbol source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<RoadMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::Cube,
        MaterialId::PavingSidewalk,
        false,
        color,
        glm::vec4(
            static_cast<float>(MaterialId::PavingSidewalk),
            kSidewalkPavingUvScale,
            kSidewalkPavingNormalStrength,
            kSidewalkPavingAoStrength));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_road_surface(float world_x, float world_z,
    const RoadSurfaceMetrics& metrics, SourceSymbol source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<RoadSurfaceMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::RoadSurface,
        MaterialId::AsphaltRoad,
        false,
        glm::vec4(1.0f),
        glm::vec4(static_cast<float>(MaterialId::AsphaltRoad), metrics.uv_scale, metrics.normal_strength, metrics.ao_strength));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_route_segment(float world_x, float world_z,
    const RouteSegmentMetrics& metrics, const glm::vec4& color, SourceSymbol source, float elevation,
    RouteLink route_link)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<RouteSegmentMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, MeshId::Cube, MaterialId::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    if (!route_link.source_qualified_name.empty() || !route_link.target_qualified_name.empty())
        registry_.emplace<RouteLink>(entity, std::move(route_link));
    return entity;
}

entt::entity SceneWorld::create_module_surface(float world_x, float world_z,
    const ModuleSurfaceMetrics& metrics, const glm::vec4& color, SourceSymbol source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<ModuleSurfaceMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::Cube,
        MaterialId::FlatColor,
        false,
        color,
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_sign(float world_x, float world_z, float elevation,
    const SignMetrics& metrics, MeshId mesh, const glm::vec4& color, SourceSymbol source,
    std::shared_ptr<const GeometryMesh> custom_mesh)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<SignMetrics>(entity, metrics);
    const MeshId mesh_id = custom_mesh ? MeshId::Custom : mesh;
    registry_.emplace<Appearance>(entity, mesh_id, MaterialId::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (custom_mesh)
        registry_.emplace<CustomMeshRef>(entity, std::move(custom_mesh));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

glm::vec3 SceneWorld::grid_to_world(float x, float z, float elevation) const
{
    return {
        (x + 0.5f) * tile_size_,
        elevation,
        (z + 0.5f) * tile_size_,
    };
}

} // namespace draxul

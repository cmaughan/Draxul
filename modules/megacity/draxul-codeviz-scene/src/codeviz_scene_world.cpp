#include <draxul/codeviz_scene_world.h>

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

CodeVizSceneWorld::CodeVizSceneWorld() = default;

void CodeVizSceneWorld::clear()
{
    registry_.clear();
}

void CodeVizSceneWorld::clear_link_segments()
{
    PERF_MEASURE();
    std::vector<entt::entity> entities;
    auto view = registry_.view<LinkSegmentMetrics>();
    for (const entt::entity entity : view)
        entities.push_back(entity);
    for (const entt::entity entity : entities)
        registry_.destroy(entity);
}

void CodeVizSceneWorld::clear_route_segments()
{
    clear_link_segments();
}

entt::entity CodeVizSceneWorld::create_block(float world_x, float world_z, float elevation,
    const BlockMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source,
    MaterialId material, std::shared_ptr<const GeometryMesh> custom_mesh, float flat_metallic,
    CustomMeshTransformMode custom_mesh_transform_mode)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<BlockMetrics>(entity, metrics);
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
        registry_.emplace<CustomMeshRef>(entity, std::move(custom_mesh), custom_mesh_transform_mode);
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_foliage_bark(float world_x, float world_z, float elevation,
    const FoliageMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<FoliageMetrics>(entity, metrics);
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
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_foliage_leaves(float world_x, float world_z, float elevation,
    const FoliageMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<FoliageMetrics>(entity, metrics);
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
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_strip(float world_x, float world_z,
    const StripMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<StripMetrics>(entity, metrics);
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
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_textured_surface(float world_x, float world_z,
    const TexturedSurfaceMetrics& metrics, CodeVizSemanticRef source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<TexturedSurfaceMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::RoadSurface,
        MaterialId::AsphaltRoad,
        false,
        glm::vec4(1.0f),
        glm::vec4(static_cast<float>(MaterialId::AsphaltRoad), metrics.uv_scale, metrics.normal_strength, metrics.ao_strength));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_link_segment(float world_x, float world_z,
    const LinkSegmentMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source, float elevation,
    RelationshipLink relationship_link)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<LinkSegmentMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, MeshId::Cube, MaterialId::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    if (!relationship_link.source_qualified_name.empty() || !relationship_link.target_qualified_name.empty())
        registry_.emplace<RelationshipLink>(entity, std::move(relationship_link));
    return entity;
}

entt::entity CodeVizSceneWorld::create_region_surface(float world_x, float world_z,
    const RegionSurfaceMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source, float elevation)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<RegionSurfaceMetrics>(entity, metrics);
    registry_.emplace<Appearance>(
        entity,
        MeshId::Cube,
        MaterialId::FlatColor,
        false,
        color,
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_ellipsoid(float world_x, float world_z, float elevation,
    const EllipsoidMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source,
    std::shared_ptr<const GeometryMesh> custom_mesh, bool double_sided)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<EllipsoidMetrics>(entity, metrics);
    const MeshId mesh_id = custom_mesh ? MeshId::Custom : MeshId::Cube;
    registry_.emplace<Appearance>(
        entity,
        mesh_id,
        MaterialId::FlatColor,
        double_sided,
        color,
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (custom_mesh)
        registry_.emplace<CustomMeshRef>(entity, std::move(custom_mesh), CustomMeshTransformMode::Baked);
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

entt::entity CodeVizSceneWorld::create_label_panel(float world_x, float world_z, float elevation,
    const LabelPanelMetrics& metrics, MeshId mesh, const glm::vec4& color, CodeVizSemanticRef source,
    std::shared_ptr<const GeometryMesh> custom_mesh, CustomMeshTransformMode custom_mesh_transform_mode)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<LabelPanelMetrics>(entity, metrics);
    const MeshId mesh_id = custom_mesh ? MeshId::Custom : mesh;
    registry_.emplace<Appearance>(entity, mesh_id, MaterialId::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    if (custom_mesh)
        registry_.emplace<CustomMeshRef>(entity, std::move(custom_mesh), custom_mesh_transform_mode);
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<CodeVizSemanticRef>(entity, std::move(source));
    return entity;
}

glm::vec3 CodeVizSceneWorld::grid_to_world(float x, float z, float elevation) const
{
    return {
        (x + 0.5f) * tile_size_,
        elevation,
        (z + 0.5f) * tile_size_,
    };
}

} // namespace draxul

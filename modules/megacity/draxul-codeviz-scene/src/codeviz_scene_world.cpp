#include <draxul/codeviz_scene_world.h>

#include <draxul/perf_timing.h>

namespace draxul
{

namespace
{

constexpr float kVertexTintPbr0UvScale = 0.45f;
constexpr float kVertexTintPbr0NormalStrength = 0.7f;
constexpr float kVertexTintPbr0AoStrength = 0.45f;
constexpr float kTexturedPbr2UvScale = 1.0f;
constexpr float kTexturedPbr2NormalStrength = 0.6f;
constexpr float kTexturedPbr2AoStrength = 0.28f;
constexpr float kTexturedPbr1UvScale = 0.10625f;
constexpr float kTexturedPbr1NormalStrength = 0.8f;
constexpr float kTexturedPbr1AoStrength = 0.55f;
constexpr float kAlphaMaskedPbr0UvScale = 1.0f;
constexpr float kAlphaMaskedPbr0NormalStrength = 0.55f;
constexpr float kAlphaMaskedPbr0ScatteringStrength = 0.85f;

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
    CodeVizMaterialPreset material, std::shared_ptr<const GeometryMesh> custom_mesh, float flat_metallic,
    CustomMeshTransformMode custom_mesh_transform_mode)
{
    PERF_MEASURE();
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<BlockMetrics>(entity, metrics);
    const MeshId mesh_id = custom_mesh ? MeshId::Custom : MeshId::Cube;
    if (material == CodeVizMaterialPreset::VertexTintPbr0)
    {
        registry_.emplace<Appearance>(
            entity,
            mesh_id,
            CodeVizMaterialPreset::VertexTintPbr0,
            false,
            color,
            glm::vec4(
                static_cast<float>(CodeVizMaterialPreset::VertexTintPbr0),
                kVertexTintPbr0UvScale,
                kVertexTintPbr0NormalStrength,
                kVertexTintPbr0AoStrength));
    }
    else if (material == CodeVizMaterialPreset::TexturedPbr1)
    {
        registry_.emplace<Appearance>(
            entity,
            mesh_id,
            CodeVizMaterialPreset::TexturedPbr1,
            false,
            color,
            glm::vec4(
                static_cast<float>(CodeVizMaterialPreset::TexturedPbr1),
                kTexturedPbr1UvScale,
                kTexturedPbr1NormalStrength,
                kTexturedPbr1AoStrength));
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
        CodeVizMaterialPreset::TexturedPbr2,
        false,
        color,
        glm::vec4(
            static_cast<float>(CodeVizMaterialPreset::TexturedPbr2),
            kTexturedPbr2UvScale,
            kTexturedPbr2NormalStrength,
            kTexturedPbr2AoStrength));
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
        CodeVizMaterialPreset::AlphaMaskedPbr0,
        false,
        color,
        glm::vec4(
            static_cast<float>(CodeVizMaterialPreset::AlphaMaskedPbr0),
            kAlphaMaskedPbr0UvScale,
            kAlphaMaskedPbr0NormalStrength,
            kAlphaMaskedPbr0ScatteringStrength));
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
        CodeVizMaterialPreset::TexturedPbr1,
        false,
        color,
        glm::vec4(
            static_cast<float>(CodeVizMaterialPreset::TexturedPbr1),
            kTexturedPbr1UvScale,
            kTexturedPbr1NormalStrength,
            kTexturedPbr1AoStrength));
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
        CodeVizMaterialPreset::TexturedPbr0,
        false,
        glm::vec4(1.0f),
        glm::vec4(
            static_cast<float>(CodeVizMaterialPreset::TexturedPbr0),
            metrics.uv_scale,
            metrics.normal_strength,
            metrics.ao_strength));
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
    registry_.emplace<Appearance>(entity, MeshId::Cube, CodeVizMaterialPreset::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
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
        CodeVizMaterialPreset::FlatColor,
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
        CodeVizMaterialPreset::FlatColor,
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
    registry_.emplace<Appearance>(entity, mesh_id, CodeVizMaterialPreset::FlatColor, false, color, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
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

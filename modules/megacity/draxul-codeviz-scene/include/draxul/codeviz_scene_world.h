#pragma once

#include <draxul/codeviz_scene_components.h>
#include <entt/entt.hpp>
#include <utility>

namespace draxul
{

// ECS-backed world. Entities are stored in an EnTT registry;
// the world itself has no fixed bounds.
class CodeVizSceneWorld
{
public:
    static constexpr float kDefaultTileSize = 1.0f;

    CodeVizSceneWorld();

    void clear();
    void clear_link_segments();
    void clear_route_segments();

    // --- Entity creation helpers ---

    // Create a block entity at the given world-space center position.
    entt::entity create_block(float world_x, float world_z, float elevation,
        const BlockMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {},
        MaterialId material = MaterialId::WoodBuilding,
        std::shared_ptr<const GeometryMesh> custom_mesh = nullptr,
        float flat_metallic = 0.0f,
        CustomMeshTransformMode custom_mesh_transform_mode = CustomMeshTransformMode::Baked);

    // Create a foliage bark/stem entity at the given world-space center position.
    entt::entity create_foliage_bark(float world_x, float world_z, float elevation,
        const FoliageMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {});

    // Create a foliage leaf/canopy entity at the given world-space center position.
    entt::entity create_foliage_leaves(float world_x, float world_z, float elevation,
        const FoliageMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {});

    // Create a strip entity at the given world-space center position.
    entt::entity create_strip(float world_x, float world_z, const StripMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f);

    // Create a textured surface cuboid at the given world-space center position.
    entt::entity create_textured_surface(float world_x, float world_z, const TexturedSurfaceMetrics& metrics,
        CodeVizSemanticRef source = {}, float elevation = 0.0f);

    // Create a thin relationship segment at the given world-space center position.
    entt::entity create_link_segment(float world_x, float world_z, const LinkSegmentMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f,
        RelationshipLink relationship_link = {});

    // Create a thin colored surface spanning a region's extents.
    entt::entity create_region_surface(float world_x, float world_z, const RegionSurfaceMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f);

    // Create an ellipsoid centered horizontally at the given position.
    entt::entity create_ellipsoid(float world_x, float world_z, float elevation,
        const EllipsoidMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {},
        std::shared_ptr<const GeometryMesh> custom_mesh = nullptr, bool double_sided = false);

    // Create a label panel entity at the given world-space center position.
    entt::entity create_label_panel(float world_x, float world_z, float elevation,
        const LabelPanelMetrics& metrics, MeshId mesh, const glm::vec4& color, CodeVizSemanticRef source = {},
        std::shared_ptr<const GeometryMesh> custom_mesh = nullptr,
        CustomMeshTransformMode custom_mesh_transform_mode = CustomMeshTransformMode::Baked);

    // City-metaphor compatibility wrappers.
    entt::entity create_building(float world_x, float world_z, float elevation,
        const BuildingMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {},
        MaterialId material = MaterialId::WoodBuilding,
        std::shared_ptr<const GeometryMesh> custom_mesh = nullptr,
        float flat_metallic = 0.0f,
        CustomMeshTransformMode custom_mesh_transform_mode = CustomMeshTransformMode::Baked)
    {
        return create_block(
            world_x, world_z, elevation, metrics, color, std::move(source), material,
            std::move(custom_mesh), flat_metallic, custom_mesh_transform_mode);
    }

    entt::entity create_tree_bark(float world_x, float world_z, float elevation,
        const TreeMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {})
    {
        return create_foliage_bark(world_x, world_z, elevation, metrics, color, std::move(source));
    }

    entt::entity create_tree_leaves(float world_x, float world_z, float elevation,
        const TreeMetrics& metrics, const glm::vec4& color, CodeVizSemanticRef source = {})
    {
        return create_foliage_leaves(world_x, world_z, elevation, metrics, color, std::move(source));
    }

    entt::entity create_road(float world_x, float world_z, const RoadMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f)
    {
        return create_strip(world_x, world_z, metrics, color, std::move(source), elevation);
    }

    entt::entity create_road_surface(float world_x, float world_z, const RoadSurfaceMetrics& metrics,
        CodeVizSemanticRef source = {}, float elevation = 0.0f)
    {
        return create_textured_surface(world_x, world_z, metrics, std::move(source), elevation);
    }

    entt::entity create_route_segment(float world_x, float world_z, const RouteSegmentMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f,
        RouteLink route_link = {})
    {
        return create_link_segment(
            world_x, world_z, metrics, color, std::move(source), elevation, std::move(route_link));
    }

    entt::entity create_module_surface(float world_x, float world_z, const ModuleSurfaceMetrics& metrics,
        const glm::vec4& color, CodeVizSemanticRef source = {}, float elevation = 0.0f)
    {
        return create_region_surface(world_x, world_z, metrics, color, std::move(source), elevation);
    }

    entt::entity create_sign(float world_x, float world_z, float elevation,
        const SignMetrics& metrics, MeshId mesh, const glm::vec4& color, CodeVizSemanticRef source = {},
        std::shared_ptr<const GeometryMesh> custom_mesh = nullptr,
        CustomMeshTransformMode custom_mesh_transform_mode = CustomMeshTransformMode::Baked)
    {
        return create_label_panel(
            world_x, world_z, elevation, metrics, mesh, color, std::move(source),
            std::move(custom_mesh), custom_mesh_transform_mode);
    }

    // --- Coordinate helpers ---

    // Convert grid coordinates to world-space position.
    glm::vec3 grid_to_world(float x, float z, float elevation = 0.0f) const;

    float tile_size() const
    {
        return tile_size_;
    }

    entt::registry& registry()
    {
        return registry_;
    }
    const entt::registry& registry() const
    {
        return registry_;
    }

private:
    float tile_size_ = kDefaultTileSize;
    entt::registry registry_;
};

} // namespace draxul

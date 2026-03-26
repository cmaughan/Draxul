#pragma once

#include "scene_components.h"
#include <entt/entt.hpp>

namespace draxul
{

// ECS-backed world. Entities are stored in an EnTT registry;
// the world itself has no fixed bounds.
class SceneWorld
{
public:
    static constexpr float kDefaultTileSize = 1.0f;

    SceneWorld();

    void clear();

    // --- Entity creation helpers ---

    // Create a building entity at the given world-space center position.
    entt::entity create_building(float world_x, float world_z, float elevation,
        const BuildingMetrics& metrics, const glm::vec4& color, SourceSymbol source = {});

    // Create a tree entity at the given world-space center position.
    entt::entity create_tree(float world_x, float world_z, const TreeMetrics& metrics,
        const glm::vec4& color, SourceSymbol source = {});

    // Create a road-strip entity at the given world-space center position.
    entt::entity create_road(float world_x, float world_z, const RoadMetrics& metrics,
        const glm::vec4& color, SourceSymbol source = {}, float elevation = 0.0f);

    // Create a sign entity at the given world-space center position.
    entt::entity create_sign(float world_x, float world_z, float elevation,
        const SignMetrics& metrics, MeshId mesh, const glm::vec4& color, SourceSymbol source = {});

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

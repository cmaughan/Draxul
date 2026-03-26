#include "scene_world.h"

namespace draxul
{

SceneWorld::SceneWorld()
{
}

void SceneWorld::clear()
{
    registry_.clear();
}

entt::entity SceneWorld::create_building(float world_x, float world_z, float elevation,
    const BuildingMetrics& metrics, const glm::vec4& color, SourceSymbol source)
{
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<BuildingMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, MeshId::Cube, color);
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_tree(float world_x, float world_z,
    const TreeMetrics& metrics, const glm::vec4& color, SourceSymbol source)
{
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, 0.0f);
    registry_.emplace<TreeMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, MeshId::Cube, color); // TODO: tree mesh
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_road(float world_x, float world_z,
    const RoadMetrics& metrics, const glm::vec4& color, SourceSymbol source, float elevation)
{
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<RoadMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, MeshId::Cube, color);
    if (!source.file.empty() || !source.name.empty())
        registry_.emplace<SourceSymbol>(entity, std::move(source));
    return entity;
}

entt::entity SceneWorld::create_sign(float world_x, float world_z, float elevation,
    const SignMetrics& metrics, MeshId mesh, const glm::vec4& color, SourceSymbol source)
{
    const auto entity = registry_.create();
    registry_.emplace<WorldPosition>(entity, world_x, world_z);
    registry_.emplace<Elevation>(entity, elevation);
    registry_.emplace<SignMetrics>(entity, metrics);
    registry_.emplace<Appearance>(entity, mesh, color);
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

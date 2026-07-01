#pragma once

#include "codeviz_scene_types.h"
#include <memory>

namespace draxul
{

class IsometricCamera;
class CodeVizSceneWorld;
struct MegaCityCodeConfig;
struct LiveCityMetricsSnapshot;
struct SignLabelAtlas;

struct CodeVizSceneSnapshotResult
{
    CodeVizSceneSnapshot snapshot;
    float world_span = 5.0f;
};

// Build a render-ready CodeVizSceneSnapshot by querying the ECS world and camera state.
CodeVizSceneSnapshotResult build_scene_snapshot(
    const IsometricCamera& camera,
    const CodeVizSceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<const LiveCityMetricsSnapshot>& live_metrics,
    const std::shared_ptr<SignLabelAtlas>& label_atlas,
    const std::shared_ptr<const MeshData>& tree_bark_mesh,
    const std::shared_ptr<const MeshData>& tree_leaf_mesh);

// Re-sort objects in an existing snapshot: opaque first, then transparent back-to-front.
// Call after modifying CodeVizRenderable::color.a in-place (e.g. selection opacity changes).
void sort_scene_objects(CodeVizSceneSnapshot& scene);

} // namespace draxul

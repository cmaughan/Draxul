#pragma once

#include <memory>

namespace draxul
{

class CodeVizScenePass;
class CodeVizSceneWorld;
class IsometricCamera;
struct GeometryMesh;
struct LiveCityMetricsSnapshot;
struct MegaCityCodeConfig;
struct SignLabelAtlas;

float publish_scene_snapshot(
    CodeVizScenePass& scene_pass,
    const IsometricCamera& camera,
    const CodeVizSceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<const LiveCityMetricsSnapshot>& live_metrics,
    const std::shared_ptr<SignLabelAtlas>& label_atlas,
    const std::shared_ptr<const GeometryMesh>& foliage_stem_mesh,
    const std::shared_ptr<const GeometryMesh>& foliage_card_mesh,
    bool preserve_tooltip);

} // namespace draxul

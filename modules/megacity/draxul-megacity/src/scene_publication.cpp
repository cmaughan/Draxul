#include "scene_publication.h"

#include "scene_snapshot_builder.h"

#include <draxul/codeviz_scene_pass.h>

namespace draxul
{

float publish_scene_snapshot(
    CodeVizScenePass& scene_pass,
    const IsometricCamera& camera,
    const CodeVizSceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<const LiveCityMetricsSnapshot>& live_metrics,
    const std::shared_ptr<SignLabelAtlas>& label_atlas,
    const std::shared_ptr<const GeometryMesh>& foliage_stem_mesh,
    const std::shared_ptr<const GeometryMesh>& foliage_card_mesh,
    bool preserve_tooltip)
{
    auto result = build_scene_snapshot(
        camera,
        world,
        config,
        live_metrics,
        label_atlas,
        foliage_stem_mesh,
        foliage_card_mesh);

    if (preserve_tooltip)
    {
        const TooltipOverlay& tooltip = scene_pass.scene().tooltip;
        if (tooltip.valid())
            result.snapshot.tooltip = tooltip;
    }

    scene_pass.set_scene(std::move(result.snapshot));
    return result.world_span;
}

} // namespace draxul

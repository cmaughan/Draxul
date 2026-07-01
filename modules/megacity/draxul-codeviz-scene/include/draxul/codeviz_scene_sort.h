#pragma once

#include <draxul/codeviz_scene_types.h>

namespace draxul
{

// Re-sort objects in an existing snapshot: opaque first, then transparent back-to-front.
// Call after modifying CodeVizRenderable::color.a in-place.
void sort_scene_objects(CodeVizSceneSnapshot& scene);

} // namespace draxul

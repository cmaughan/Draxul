#pragma once

#include "satview_scene_pass.h"

#include <vector>

namespace draxul::satview
{

[[nodiscard]] std::vector<SatViewSceneVertex> load_satview_constellation_catalog();

} // namespace draxul::satview

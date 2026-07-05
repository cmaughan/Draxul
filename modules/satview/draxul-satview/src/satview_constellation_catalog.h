#pragma once

#include "satview_scene_pass.h"

#include <vector>

namespace draxul::satview
{

[[nodiscard]] std::vector<SatViewCelestialLineInstance> load_satview_constellation_catalog();

} // namespace draxul::satview

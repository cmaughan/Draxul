#pragma once

#include "satview_scene_pass.h"

#include <cstddef>
#include <vector>

namespace draxul::satview
{

[[nodiscard]] std::vector<SatViewStarInstance> load_satview_star_catalog(std::size_t maximum_count);

} // namespace draxul::satview

#pragma once

#include "satview_scene_pass.h"

#include <filesystem>
#include <vector>

namespace draxul::satview
{

[[nodiscard]] std::vector<SatViewCelestialLineInstance> load_satview_constellation_catalog();
[[nodiscard]] std::vector<SatViewCelestialLineInstance> load_satview_constellation_catalog(
    const std::filesystem::path& path);

} // namespace draxul::satview

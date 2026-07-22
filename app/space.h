#pragma once

#include "tab_controller.h"

#include <filesystem>
#include <string>

namespace draxul
{

using SpaceId = int;
inline constexpr SpaceId kInvalidSpaceId = -1;
inline constexpr SpaceId kDefaultSpaceId = 0;

// A project/task container within the running Draxul session. Phase 0 exposes
// one default Space; later phases can add creation and switching without
// changing TabController ownership again.
struct Space
{
    SpaceId id = kInvalidSpaceId;
    std::string name;
    std::filesystem::path root_directory;
    TabController tab_controller;
};

} // namespace draxul

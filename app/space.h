#pragma once

#include "tab_controller.h"

#include <filesystem>
#include <string>

namespace draxul
{

using SpaceId = int;
inline constexpr SpaceId kInvalidSpaceId = -1;
inline constexpr SpaceId kDefaultSpaceId = 0;

// A project/task container within the running Draxul session. Each Space owns
// its tabs and remains live while another Space is active.
struct Space
{
    SpaceId id = kInvalidSpaceId;
    std::string name;
    std::filesystem::path root_directory;
    TabController tab_controller;
};

} // namespace draxul

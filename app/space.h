#pragma once

#include "space_id.h"
#include "tab_controller.h"

#include <filesystem>
#include <string>

namespace draxul
{

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

#include "space_controller.h"

#include <draxul/log.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace draxul
{

SpaceController::SpaceController(std::filesystem::path default_root_directory)
{
    auto space = std::make_unique<Space>();
    space->id = next_space_id_++;
    space->name = "default";
    space->root_directory = std::move(default_root_directory);
    active_space_id_ = space->id;
    spaces_.push_back(std::move(space));
}

Space* SpaceController::find_active_space() noexcept
{
    for (auto& space : spaces_)
    {
        if (space->id == active_space_id_)
            return space.get();
    }
    return nullptr;
}

const Space* SpaceController::find_active_space() const noexcept
{
    for (const auto& space : spaces_)
    {
        if (space->id == active_space_id_)
            return space.get();
    }
    return nullptr;
}

Space& SpaceController::require_active_space(std::string_view context)
{
    if (Space* space = find_active_space())
        return *space;
    DRAXUL_LOG_ERROR(LogCategory::App,
        "Active space invariant failed in %.*s (active_id=%d count=%zu)",
        static_cast<int>(context.size()), context.data(), active_space_id_, spaces_.size());
    throw std::logic_error("Draxul active space invariant failed in " + std::string(context));
}

const Space& SpaceController::require_active_space(std::string_view context) const
{
    if (const Space* space = find_active_space())
        return *space;
    DRAXUL_LOG_ERROR(LogCategory::App,
        "Active space invariant failed in %.*s (active_id=%d count=%zu)",
        static_cast<int>(context.size()), context.data(), active_space_id_, spaces_.size());
    throw std::logic_error("Draxul active space invariant failed in " + std::string(context));
}

TabController& SpaceController::active_tab_controller()
{
    return require_active_space("active_tab_controller").tab_controller;
}

const TabController& SpaceController::active_tab_controller() const
{
    return require_active_space("active_tab_controller const").tab_controller;
}

void SpaceController::shutdown_all()
{
    for (auto& space : spaces_)
        space->tab_controller.shutdown_all();
}

} // namespace draxul

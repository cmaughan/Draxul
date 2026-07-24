#include "space_controller.h"

#include <algorithm>
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

Space* SpaceController::find_space(SpaceId id) noexcept
{
    const auto it = std::find_if(spaces_.begin(), spaces_.end(),
        [id](const auto& space) { return space->id == id; });
    return it != spaces_.end() ? it->get() : nullptr;
}

const Space* SpaceController::find_space(SpaceId id) const noexcept
{
    const auto it = std::find_if(spaces_.begin(), spaces_.end(),
        [id](const auto& space) { return space->id == id; });
    return it != spaces_.end() ? it->get() : nullptr;
}

Space* SpaceController::find_active_space() noexcept
{
    return find_space(active_space_id_);
}

const Space* SpaceController::find_active_space() const noexcept
{
    return find_space(active_space_id_);
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

SpaceId SpaceController::create_space(std::string name, std::filesystem::path root_directory)
{
    if (name.empty())
        return kInvalidSpaceId;

    auto space = std::make_unique<Space>();
    space->id = next_space_id_++;
    space->name = std::move(name);
    space->root_directory = std::move(root_directory);
    space->tab_controller.set_focus_enabled(false);
    const SpaceId id = space->id;
    spaces_.push_back(std::move(space));
    return id;
}

bool SpaceController::activate_space(SpaceId id)
{
    Space* target = find_space(id);
    if (!target)
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "Cannot activate missing space id=%d", id);
        return false;
    }
    if (id == active_space_id_)
        return true;
    if (!target->tab_controller.find_active_tab())
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "Cannot activate empty space id=%d", id);
        return false;
    }

    if (Space* current = find_active_space())
        current->tab_controller.set_focus_enabled(false);
    active_space_id_ = id;
    target->tab_controller.set_focus_enabled(true);
    return true;
}

bool SpaceController::rename_space(SpaceId id, std::string name)
{
    Space* space = find_space(id);
    if (!space || name.empty())
        return false;
    space->name = std::move(name);
    return true;
}

bool SpaceController::set_space_root_directory(
    SpaceId id, std::filesystem::path root_directory)
{
    Space* space = find_space(id);
    if (!space)
        return false;
    space->root_directory = std::move(root_directory);
    return true;
}

bool SpaceController::close_space(SpaceId id)
{
    if (spaces_.size() <= 1)
        return false;

    const auto closing = std::find_if(spaces_.begin(), spaces_.end(),
        [id](const auto& space) { return space->id == id; });
    if (closing == spaces_.end())
        return false;

    if (id == active_space_id_)
    {
        const auto replacement = std::find_if(spaces_.begin(), spaces_.end(),
            [id](const auto& space) {
                return space->id != id && space->tab_controller.find_active_tab() != nullptr;
            });
        if (replacement == spaces_.end())
            return false;

        // Transfer focus while both Space trees are still alive.
        if (!activate_space((*replacement)->id))
            return false;
    }

    (*closing)->tab_controller.shutdown_all();
    spaces_.erase(closing);
    return true;
}

bool SpaceController::all_spaces_restorable() const
{
    if (spaces_.empty())
        return false;
    return std::all_of(spaces_.begin(), spaces_.end(), [](const auto& space) {
        return space->tab_controller.all_tabs_restorable();
    });
}

std::optional<std::vector<SpaceSnapshot>> SpaceController::snapshot_spaces() const
{
    std::vector<SpaceSnapshot> snapshots;
    snapshots.reserve(spaces_.size());
    for (const auto& space : spaces_)
    {
        auto tabs = space->tab_controller.snapshot_tabs();
        if (!tabs)
            return std::nullopt;

        SpaceSnapshot snapshot;
        snapshot.id = space->id;
        snapshot.name = space->name;
        snapshot.root_directory = space->root_directory;
        snapshot.active_tab_id = space->tab_controller.active_tab_id();
        snapshot.next_tab_id = space->tab_controller.next_tab_id();
        snapshot.tabs = std::move(*tabs);
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

void SpaceController::shutdown_all()
{
    for (auto& space : spaces_)
        space->tab_controller.shutdown_all();
}

} // namespace draxul

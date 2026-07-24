#include "space_controller.h"

#include <algorithm>
#include <draxul/log.h>
#include <stdexcept>
#include <string>
#include <unordered_set>
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

bool SpaceController::restore_spaces(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    const std::vector<SpaceSnapshot>& snapshots, SpaceId restored_active_space_id,
    SpaceId restored_next_space_id, const PaneManagerDepsFactory& make_pane_manager_deps)
{
    last_restore_error_.clear();
    last_restore_warning_.clear();
    if (snapshots.empty() || !make_pane_manager_deps)
    {
        last_restore_error_ = "Saved session has no Spaces.";
        return false;
    }

    std::unordered_set<SpaceId> restored_ids;
    SpaceId max_space_id = kInvalidSpaceId;
    for (const SpaceSnapshot& snapshot : snapshots)
    {
        if (snapshot.id < kDefaultSpaceId || !restored_ids.insert(snapshot.id).second)
        {
            last_restore_error_ = "Saved session contains a duplicate or invalid Space id.";
            return false;
        }
        max_space_id = std::max(max_space_id, snapshot.id);
    }

    Spaces candidate_spaces;
    candidate_spaces.reserve(snapshots.size());
    std::vector<std::string> warnings;
    const auto shutdown_candidates = [&candidate_spaces]() {
        for (auto& space : candidate_spaces)
            space->tab_controller.shutdown_all();
        candidate_spaces.clear();
    };

    for (const SpaceSnapshot& snapshot : snapshots)
    {
        auto space = std::make_unique<Space>();
        space->id = snapshot.id;
        space->name = snapshot.name.empty() ? "default" : snapshot.name;
        space->root_directory = snapshot.root_directory;
        space->tab_controller.set_focus_enabled(false);

        std::vector<std::string> tab_warnings;
        if (!space->tab_controller.restore_tabs(callbacks, pixel_w, pixel_h,
                snapshot.tabs, snapshot.active_tab_id, snapshot.next_tab_id,
                [&make_pane_manager_deps, candidate = space.get()]() {
                    return make_pane_manager_deps(candidate);
                },
                TabController::RestorePolicy::RecoverUsableTabs, &tab_warnings))
        {
            const std::string reason = space->tab_controller.last_error().empty()
                ? "no restorable tabs"
                : space->tab_controller.last_error();
            warnings.push_back(
                "Space " + std::to_string(snapshot.id) + " was skipped: " + reason);
            space->tab_controller.shutdown_all();
            continue;
        }

        for (std::string& warning : tab_warnings)
        {
            warnings.push_back(
                "Space " + std::to_string(snapshot.id) + " " + std::move(warning));
        }
        candidate_spaces.push_back(std::move(space));
    }

    if (candidate_spaces.empty())
    {
        shutdown_candidates();
        last_restore_error_ = "Saved session has no restorable Spaces.";
        if (!warnings.empty())
            last_restore_error_ += " " + warnings.front();
        return false;
    }

    const auto requested_active = std::find_if(candidate_spaces.begin(), candidate_spaces.end(),
        [restored_active_space_id](const auto& space) {
            return space->id == restored_active_space_id;
        });
    const SpaceId active_id = requested_active != candidate_spaces.end()
        ? restored_active_space_id
        : candidate_spaces.front()->id;
    if (requested_active == candidate_spaces.end())
    {
        warnings.push_back("the saved active Space was unavailable; the first restored Space was selected");
    }

    if (Space* active = find_active_space())
        active->tab_controller.set_focus_enabled(false);
    shutdown_all();
    spaces_ = std::move(candidate_spaces);
    active_space_id_ = kInvalidSpaceId;
    next_space_id_ = std::max(restored_next_space_id, max_space_id + 1);
    if (!activate_space(active_id))
    {
        shutdown_all();
        spaces_.clear();
        last_restore_error_ = "Failed to activate a restored Space.";
        return false;
    }

    for (size_t i = 0; i < warnings.size(); ++i)
    {
        if (i != 0)
            last_restore_warning_ += "; ";
        last_restore_warning_ += warnings[i];
    }
    return true;
}

void SpaceController::shutdown_all()
{
    for (auto& space : spaces_)
        space->tab_controller.shutdown_all();
}

} // namespace draxul

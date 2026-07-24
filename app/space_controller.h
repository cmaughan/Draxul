#pragma once

#include "space.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

// Owns the live Spaces in one Draxul session. Spaces remain resident when
// inactive; activating a Space transfers host focus without disturbing its
// tabs or processes.
class SpaceController
{
public:
    using Spaces = std::vector<std::unique_ptr<Space>>;
    using PaneManagerDepsFactory = std::function<PaneManager::Deps(const Space*)>;

    explicit SpaceController(std::filesystem::path default_root_directory = {});
    SpaceController(SpaceController&&) noexcept = default;
    SpaceController& operator=(SpaceController&&) noexcept = default;
    SpaceController(const SpaceController&) = delete;
    SpaceController& operator=(const SpaceController&) = delete;

    Space* find_space(SpaceId id) noexcept;
    const Space* find_space(SpaceId id) const noexcept;
    Space* find_active_space() noexcept;
    const Space* find_active_space() const noexcept;
    Space& require_active_space(std::string_view context);
    const Space& require_active_space(std::string_view context) const;

    TabController& active_tab_controller();
    const TabController& active_tab_controller() const;

    SpaceId create_space(std::string name, std::filesystem::path root_directory = {});
    bool activate_space(SpaceId id);
    bool rename_space(SpaceId id, std::string name);
    bool set_space_root_directory(SpaceId id, std::filesystem::path root_directory);
    bool close_space(SpaceId id);

    bool all_spaces_restorable() const;
    std::optional<std::vector<SpaceSnapshot>> snapshot_spaces() const;
    bool restore_spaces(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        const std::vector<SpaceSnapshot>& snapshots, SpaceId restored_active_space_id,
        SpaceId restored_next_space_id, const PaneManagerDepsFactory& make_pane_manager_deps);
    void shutdown_all();

    const Spaces& spaces() const noexcept
    {
        return spaces_;
    }
    int count() const noexcept
    {
        return static_cast<int>(spaces_.size());
    }
    SpaceId active_space_id() const noexcept
    {
        return active_space_id_;
    }
    SpaceId next_space_id() const noexcept
    {
        return next_space_id_;
    }
    const std::string& last_restore_error() const noexcept
    {
        return last_restore_error_;
    }
    const std::string& last_restore_warning() const noexcept
    {
        return last_restore_warning_;
    }

private:
    Spaces spaces_;
    SpaceId active_space_id_ = kInvalidSpaceId;
    SpaceId next_space_id_ = kDefaultSpaceId;
    std::string last_restore_error_;
    std::string last_restore_warning_;
};

} // namespace draxul

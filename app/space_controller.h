#pragma once

#include "space.h"

#include <filesystem>
#include <memory>
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

    explicit SpaceController(std::filesystem::path default_root_directory = {});

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

private:
    Spaces spaces_;
    SpaceId active_space_id_ = kInvalidSpaceId;
    SpaceId next_space_id_ = kDefaultSpaceId;
};

} // namespace draxul

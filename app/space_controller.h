#pragma once

#include "space.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace draxul
{

// Owns the live Spaces in one Draxul session. The initial implementation
// deliberately creates exactly one default Space while establishing the
// ownership and active-Space routing boundary needed for the sidebar phase.
class SpaceController
{
public:
    using Spaces = std::vector<std::unique_ptr<Space>>;

    explicit SpaceController(std::filesystem::path default_root_directory = {});

    Space* find_active_space() noexcept;
    const Space* find_active_space() const noexcept;
    Space& require_active_space(std::string_view context);
    const Space& require_active_space(std::string_view context) const;

    TabController& active_tab_controller();
    const TabController& active_tab_controller() const;

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

private:
    Spaces spaces_;
    SpaceId active_space_id_ = kInvalidSpaceId;
    SpaceId next_space_id_ = kDefaultSpaceId;
};

} // namespace draxul

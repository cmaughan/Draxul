#pragma once

#include "session_state.h"
#include "tab.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

// Owns the tabs within one Space: their stable identities, active
// selection, pane layouts, and lifecycle. App supplies host-specific
// dependencies but does not own or manipulate the collection directly.
class TabController
{
public:
    using Tabs = std::vector<std::unique_ptr<Tab>>;
    using PaneManagerDepsFactory = std::function<PaneManager::Deps()>;

    bool create_initial_tab(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        PaneManager::Deps pane_manager_deps);
    int add_tab(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        PaneManager::Deps pane_manager_deps,
        std::optional<HostKind> host_kind = std::nullopt);

    bool close_tab(int tab_id);
    bool activate_tab(int tab_id);
    void next_tab();
    void prev_tab();
    void move_tab(int direction);
    void activate_tab_by_index(int one_based_index);

    Tab* find_active_tab() noexcept;
    const Tab* find_active_tab() const noexcept;
    Tab& require_active_tab(std::string_view context);
    const Tab& require_active_tab(std::string_view context) const;
    PaneManager& active_pane_manager();
    const PaneManager& active_pane_manager() const;
    const SplitTree& active_tree() const;

    bool all_tabs_restorable() const;
    std::optional<std::vector<TabSnapshot>> snapshot_tabs() const;
    bool restore_tabs(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        const std::vector<TabSnapshot>& snapshots, int restored_active_tab_id,
        int restored_next_tab_id, const PaneManagerDepsFactory& make_pane_manager_deps);

    void recompute_all_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h);
    void shutdown_all();

    Tabs& tabs() noexcept
    {
        return tabs_;
    }
    const Tabs& tabs() const noexcept
    {
        return tabs_;
    }
    int count() const noexcept
    {
        return static_cast<int>(tabs_.size());
    }
    bool empty() const noexcept
    {
        return tabs_.empty();
    }
    int active_tab_id() const noexcept
    {
        return active_tab_id_;
    }
    int next_tab_id() const noexcept
    {
        return next_tab_id_;
    }
    const std::string& last_error() const noexcept
    {
        return last_error_;
    }

private:
    Tabs tabs_;
    int active_tab_id_ = -1;
    int next_tab_id_ = 0;
    std::string last_error_;
};

} // namespace draxul

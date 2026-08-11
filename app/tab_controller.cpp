#include "tab_controller.h"

#include <algorithm>
#include <draxul/log.h>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

void set_default_tab_name(Tab& tab)
{
    if (IHost* host = tab.pane_manager.host())
        tab.name = host->debug_state().name;
    else
        tab.name = "tab";
}

} // namespace

bool TabController::create_initial_tab(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    PaneManager::Deps pane_manager_deps)
{
    auto tab = std::make_unique<Tab>(next_tab_id_++, std::move(pane_manager_deps));
    if (!tab->pane_manager.create(callbacks, pixel_w, pixel_h))
    {
        last_error_ = tab->pane_manager.error();
        return false;
    }

    tab->initialized = true;
    set_default_tab_name(*tab);
    const int id = tab->id;
    tabs_.push_back(std::move(tab));
    activate_tab(id);
    last_error_.clear();
    return true;
}

int TabController::add_tab(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    PaneManager::Deps pane_manager_deps, std::optional<HostKind> host_kind)
{
    auto tab = std::make_unique<Tab>(next_tab_id_++, std::move(pane_manager_deps));
    const HostKind kind = host_kind.value_or(PaneManager::platform_default_split_host_kind());
    if (!tab->pane_manager.create(callbacks, pixel_w, pixel_h, kind))
    {
        last_error_ = tab->pane_manager.error();
        return -1;
    }

    tab->initialized = true;
    set_default_tab_name(*tab);
    const int id = tab->id;
    tabs_.push_back(std::move(tab));
    activate_tab(id);
    last_error_.clear();
    return id;
}

int TabController::add_projected_tab(
    PaneManager::Deps pane_manager_deps)
{
    auto tab = std::make_unique<Tab>(
        next_tab_id_++, std::move(pane_manager_deps));
    tab->name = "tab";
    const int id = tab->id;
    tabs_.push_back(std::move(tab));
    last_error_.clear();
    return id;
}

bool TabController::close_tab(int tab_id)
{
    if (tabs_.size() <= 1)
        return false;

    auto it = std::find_if(tabs_.begin(), tabs_.end(),
        [tab_id](const auto& tab) { return tab->id == tab_id; });
    if (it == tabs_.end())
        return false;

    if (tab_id == active_tab_id_)
    {
        const auto replacement = std::find_if(tabs_.begin(), tabs_.end(),
            [tab_id](const auto& tab) { return tab->id != tab_id; });
        if (replacement == tabs_.end())
            return false;

        // Complete the focus transition while both managers remain alive.
        activate_tab((*replacement)->id);
    }

    (*it)->pane_manager.shutdown();
    tabs_.erase(it);
    return true;
}

bool TabController::activate_tab(int tab_id)
{
    if (tab_id == active_tab_id_ && find_active_tab() != nullptr)
        return true;

    const auto target = std::find_if(tabs_.begin(), tabs_.end(),
        [tab_id](const auto& tab) { return tab->id == tab_id; });
    if (target == tabs_.end())
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "Cannot activate missing tab id=%d", tab_id);
        return false;
    }

    if (focus_enabled_)
    {
        if (Tab* current = find_active_tab())
        {
            if (IHost* host = current->pane_manager.focused_host())
                host->on_focus_lost();
        }
    }

    active_tab_id_ = tab_id;
    if (focus_enabled_)
    {
        if (IHost* host = (*target)->pane_manager.focused_host())
            host->on_focus_gained();
    }
    return true;
}

void TabController::set_focus_enabled(bool enabled)
{
    if (enabled == focus_enabled_)
        return;

    if (Tab* active = find_active_tab())
    {
        if (IHost* host = active->pane_manager.focused_host())
        {
            if (enabled)
                host->on_focus_gained();
            else
                host->on_focus_lost();
        }
    }
    focus_enabled_ = enabled;
}

void TabController::next_tab()
{
    if (tabs_.size() <= 1)
        return;

    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        if (tabs_[i]->id == active_tab_id_)
        {
            activate_tab(tabs_[(i + 1) % tabs_.size()]->id);
            return;
        }
    }
}

void TabController::prev_tab()
{
    if (tabs_.size() <= 1)
        return;

    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        if (tabs_[i]->id == active_tab_id_)
        {
            const size_t previous = (i == 0) ? tabs_.size() - 1 : i - 1;
            activate_tab(tabs_[previous]->id);
            return;
        }
    }
}

void TabController::move_tab(int direction)
{
    if (tabs_.size() <= 1)
        return;

    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        if (tabs_[i]->id == active_tab_id_)
        {
            const auto count = static_cast<int>(tabs_.size());
            const int target = (static_cast<int>(i) + direction + count) % count;
            std::swap(tabs_[i], tabs_[static_cast<size_t>(target)]);
            return;
        }
    }
}

bool TabController::reorder_projected_tabs(
    const std::vector<int>& ordered_ids)
{
    if (ordered_ids.size() != tabs_.size())
        return false;

    std::unordered_set<int> current_ids;
    for (const auto& tab : tabs_)
        current_ids.insert(tab->id);
    std::unordered_set<int> requested_ids(
        ordered_ids.begin(), ordered_ids.end());
    if (current_ids != requested_ids
        || requested_ids.size() != ordered_ids.size())
    {
        return false;
    }

    Tabs reordered;
    reordered.reserve(tabs_.size());
    for (const int id : ordered_ids)
    {
        const auto found = std::find_if(
            tabs_.begin(), tabs_.end(),
            [id](const auto& tab) { return tab && tab->id == id; });
        reordered.push_back(std::move(*found));
    }
    tabs_ = std::move(reordered);
    return true;
}

void TabController::activate_tab_by_index(int one_based_index)
{
    const int index = one_based_index - 1;
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        return;
    activate_tab(tabs_[static_cast<size_t>(index)]->id);
}

Tab* TabController::find_active_tab() noexcept
{
    for (auto& tab : tabs_)
    {
        if (tab->id == active_tab_id_)
            return tab.get();
    }
    return nullptr;
}

const Tab* TabController::find_active_tab() const noexcept
{
    for (const auto& tab : tabs_)
    {
        if (tab->id == active_tab_id_)
            return tab.get();
    }
    return nullptr;
}

Tab& TabController::require_active_tab(std::string_view context)
{
    if (Tab* tab = find_active_tab())
        return *tab;
    DRAXUL_LOG_ERROR(LogCategory::App,
        "Active tab invariant failed in %.*s (active_id=%d count=%zu)",
        static_cast<int>(context.size()), context.data(), active_tab_id_, tabs_.size());
    throw std::logic_error("Draxul active tab invariant failed in " + std::string(context));
}

const Tab& TabController::require_active_tab(std::string_view context) const
{
    if (const Tab* tab = find_active_tab())
        return *tab;
    DRAXUL_LOG_ERROR(LogCategory::App,
        "Active tab invariant failed in %.*s (active_id=%d count=%zu)",
        static_cast<int>(context.size()), context.data(), active_tab_id_, tabs_.size());
    throw std::logic_error("Draxul active tab invariant failed in " + std::string(context));
}

PaneManager& TabController::active_pane_manager()
{
    return require_active_tab("active_pane_manager").pane_manager;
}

const PaneManager& TabController::active_pane_manager() const
{
    return require_active_tab("active_pane_manager const").pane_manager;
}

const SplitTree& TabController::active_tree() const
{
    return active_pane_manager().tree();
}

bool TabController::all_tabs_restorable() const
{
    if (tabs_.empty())
        return false;
    return std::all_of(tabs_.begin(), tabs_.end(), [](const auto& tab) {
        return tab->pane_manager.has_restorable_shell_session();
    });
}

std::optional<std::vector<TabSnapshot>> TabController::snapshot_tabs() const
{
    std::vector<TabSnapshot> snapshots;
    snapshots.reserve(tabs_.size());
    for (const auto& tab : tabs_)
    {
        auto pane_layout = tab->pane_manager.snapshot_layout();
        if (!pane_layout)
            return std::nullopt;

        TabSnapshot snapshot;
        snapshot.id = tab->id;
        snapshot.name = tab->name;
        snapshot.name_user_set = tab->name_user_set;
        snapshot.pane_layout = std::move(*pane_layout);
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

bool TabController::restore_tabs(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    const std::vector<TabSnapshot>& snapshots, int restored_active_tab_id,
    int restored_next_tab_id, const PaneManagerDepsFactory& make_pane_manager_deps,
    RestorePolicy policy, std::vector<std::string>* recovery_warnings)
{
    if (snapshots.empty() || !make_pane_manager_deps)
        return false;

    std::unordered_set<int> restored_ids;
    int max_tab_id = -1;
    for (const TabSnapshot& snapshot : snapshots)
    {
        if (snapshot.id < 0 || !restored_ids.insert(snapshot.id).second)
        {
            last_error_ = "Saved session contains a duplicate or invalid tab id.";
            return false;
        }
        max_tab_id = std::max(max_tab_id, snapshot.id);
    }

    Tabs candidate_tabs;
    candidate_tabs.reserve(snapshots.size());
    const auto shutdown_candidate = [&candidate_tabs]() {
        for (auto& tab : candidate_tabs)
            tab->pane_manager.shutdown();
        candidate_tabs.clear();
    };

    for (const TabSnapshot& snapshot : snapshots)
    {
        auto tab = std::make_unique<Tab>(snapshot.id, make_pane_manager_deps());
        if (!tab->pane_manager.restore_layout(callbacks, pixel_w, pixel_h, snapshot.pane_layout))
        {
            const std::string tab_error = tab->pane_manager.error().empty()
                ? "host initialization failed"
                : tab->pane_manager.error();
            tab->pane_manager.shutdown();
            if (policy == RestorePolicy::Strict)
            {
                last_error_ = tab_error;
                shutdown_candidate();
                return false;
            }
            if (recovery_warnings)
            {
                recovery_warnings->push_back(
                    "tab " + std::to_string(snapshot.id) + " was skipped: " + tab_error);
            }
            continue;
        }

        tab->initialized = true;
        tab->name = snapshot.name.empty() ? "tab" : snapshot.name;
        tab->name_user_set = snapshot.name_user_set;
        candidate_tabs.push_back(std::move(tab));
    }

    if (candidate_tabs.empty())
    {
        last_error_ = "Saved Space has no restorable tabs.";
        return false;
    }

    const bool has_restored_active_tab = std::any_of(candidate_tabs.begin(), candidate_tabs.end(),
        [restored_active_tab_id](const auto& tab) {
            return tab->id == restored_active_tab_id;
        });
    if (!has_restored_active_tab)
        restored_active_tab_id = candidate_tabs.front()->id;

    if (focus_enabled_)
    {
        if (Tab* current = find_active_tab())
        {
            if (IHost* host = current->pane_manager.focused_host())
                host->on_focus_lost();
        }
    }
    shutdown_all();
    tabs_ = std::move(candidate_tabs);
    next_tab_id_ = std::max(restored_next_tab_id, max_tab_id + 1);
    last_error_.clear();
    return activate_tab(restored_active_tab_id);
}

void TabController::recompute_all_viewports(
    int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    for (auto& tab : tabs_)
        tab->pane_manager.recompute_viewports(origin_x, origin_y, pixel_w, pixel_h);
}

void TabController::shutdown_all()
{
    for (auto& tab : tabs_)
        tab->pane_manager.shutdown();
    tabs_.clear();
    active_tab_id_ = -1;
}

} // namespace draxul

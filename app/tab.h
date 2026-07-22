#pragma once

#include "host_manager.h"
#include <string>

namespace draxul
{

// A tab is a self-contained pane layout: one SplitTree + its hosts.
// ChromeHost presents the collection as top-bar tabs.
struct Tab
{
    int id = -1;
    std::string name;
    // True once the user has explicitly renamed this tab via the inline rename
    // UI (or the rename_tab action). Default-naming from OSC 7 cwd updates is
    // suppressed once this flag is set so the user's choice is sticky.
    bool name_user_set = false;
    HostManager host_manager;
    bool initialized = false;

    explicit Tab(int tab_id, HostManager::Deps deps)
        : id(tab_id)
        , host_manager(std::move(deps))
    {
    }
};

} // namespace draxul

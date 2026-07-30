#include "session_listing.h"
#include "session_state.h"
#include "split_tree.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

namespace
{

SessionSnapshot make_saved_session(std::string session_id, std::string session_name)
{
    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    SessionSnapshot state;
    state.session_id = std::move(session_id);
    state.session_name = std::move(session_name);
    state.active_space_id = 1;
    state.next_space_id = 2;

    TabSnapshot tab;
    tab.id = 1;
    tab.name = "alpha";
    tab.name_user_set = true;
    tab.pane_layout.tree = tree.snapshot();
    tab.pane_layout.panes.push_back({
        .leaf_id = leaf,
        .launch = { .kind = HostKind::PowerShell, .command = "pwsh", .working_dir = "D:/tmp" },
        .pane_name = "shell",
        .pane_id = "pane-1",
    });
    SpaceSnapshot space;
    space.id = 1;
    space.active_tab_id = 1;
    space.next_tab_id = 2;
    space.tabs.push_back(std::move(tab));
    state.spaces.push_back(std::move(space));
    return state;
}

} // namespace

TEST_CASE("session listing reports saved session topology", "[session_listing]")
{
    TempDir temp_dir("session-listing-saved");
    HomeDirRedirect redirect(temp_dir.path);

    std::string error;
    REQUIRE(save_session_state(make_saved_session("alpha", "Alpha Session"), &error));
    REQUIRE(error.empty());

    const auto sessions = list_known_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "alpha");
    CHECK(sessions[0].session_name == "Alpha Session");
    CHECK(sessions[0].space_count == 1);
    CHECK(sessions[0].tab_count == 1);
    CHECK(sessions[0].pane_count == 1);
    CHECK(sessions[0].has_saved_state);
    CHECK(session_entry_name(sessions[0]) == "Alpha Session (alpha)");
    CHECK(session_entry_hint(sessions[0]) == "saved 1s/1t/1p");
}

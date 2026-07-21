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

AppSessionState make_saved_session(std::string session_id, std::string session_name)
{
    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    AppSessionState state;
    state.session_id = std::move(session_id);
    state.session_name = std::move(session_name);
    state.active_workspace_id = 1;
    state.next_workspace_id = 2;

    WorkspaceSessionState workspace;
    workspace.id = 1;
    workspace.name = "alpha";
    workspace.name_user_set = true;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = { .kind = HostKind::PowerShell, .command = "pwsh", .working_dir = "D:/tmp" },
        .pane_name = "shell",
        .pane_id = "pane-1",
    });
    state.workspaces.push_back(std::move(workspace));
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
    CHECK(sessions[0].workspace_count == 1);
    CHECK(sessions[0].pane_count == 1);
    CHECK(sessions[0].has_saved_state);
    CHECK(session_entry_name(sessions[0]) == "Alpha Session (alpha)");
    CHECK(session_entry_hint(sessions[0]) == "saved 1w/1p");
}

TEST_CASE("session listing formatter prints aligned saved-session table", "[session_listing]")
{
    SessionSummary alpha;
    alpha.session_id = "alpha";
    alpha.session_name = "Alpha Session";
    alpha.workspace_count = 3;
    alpha.pane_count = 12;
    alpha.has_saved_state = true;

    SessionSummary beta;
    beta.session_id = "beta";
    beta.session_name = "beta";
    beta.workspace_count = 1;
    beta.pane_count = 2;
    beta.has_saved_state = true;

    const std::string table = format_session_listing_table({ alpha, beta });
    const std::string expected
        = "SESSION ID  WORKSPACES  PANES  NAME\n"
          "----------  ----------  -----  ----\n"
          "alpha                3     12  Alpha Session\n"
          "beta                 1      2  \n";
    CHECK(table == expected);
}

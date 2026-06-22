#include "session_id.h"
#include "session_state.h"
#include "split_tree.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>

#include <cctype>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("session id: slug normalizes display names", "[session_id]")
{
    CHECK(make_session_id_slug(" Work Bench!! ") == "work-bench");
    CHECK(make_session_id_slug("...") == "session");
    CHECK(make_session_id_slug("Alpha/Beta_Gamma") == "alpha-beta-gamma");
}

TEST_CASE("session id: timestamp uses sortable local time format", "[session_id]")
{
    const std::string stamp = format_session_id_timestamp(0);

    REQUIRE(stamp.size() == 15);
    CHECK(stamp[8] == '-');
    for (size_t i = 0; i < stamp.size(); ++i)
    {
        if (i == 8)
            continue;
        CHECK(std::isdigit(static_cast<unsigned char>(stamp[i])));
    }
}

TEST_CASE("session id: candidate suffixes start at the unsuffixed base", "[session_id]")
{
    const std::string base = "work-bench-20260102-030405";

    CHECK(make_session_id_candidate(base, 1) == base);
    CHECK(make_session_id_candidate(base, 2) == base + "-2");
    CHECK(make_session_id_candidate(base, 9) == base + "-9");
}

TEST_CASE("session id: generated unique ids skip saved state collisions", "[session_id]")
{
    TempDir temp_dir("session-id-unique");
    HomeDirRedirect redirect(temp_dir.path);

    const int64_t fixed_time = 0;
    const std::string base = make_session_id_base("Work Bench", fixed_time);

    AppSessionState state;
    state.session_id = base;
    state.session_name = "Work Bench";
    state.active_workspace_id = 1;
    state.next_workspace_id = 2;

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());

    auto generated = make_unique_session_id("Work Bench", fixed_time);
    REQUIRE(generated);
    CHECK(*generated == base + "-2");
}

TEST_CASE("session state: save/load round-trip preserves workspace topology", "[session_state]")
{
    TempDir temp_dir("session-state-roundtrip");
    HomeDirRedirect redirect(temp_dir.path);

    SplitTree tree;
    const LeafId left = tree.reset(1200, 800);
    const LeafId right = tree.split_leaf(left, SplitDirection::Vertical);
    tree.set_focused(right);

    HostManager::SessionState host_manager_state;
    host_manager_state.tree = tree.snapshot();
    host_manager_state.zoomed = true;
    host_manager_state.zoomed_leaf = right;
    host_manager_state.panes.push_back({
        .leaf_id = left,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .args = { "-NoLogo" },
            .working_dir = "D:/left",
            .source_path = "",
            .startup_commands = { "echo left" },
        },
        .pane_name = "left",
        .pane_id = "pane-left",
    });
    host_manager_state.panes.push_back({
        .leaf_id = right,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .args = { "-NoProfile" },
            .working_dir = "D:/right",
            .source_path = "",
            .startup_commands = { "echo right" },
        },
        .pane_name = "right",
        .pane_id = "pane-right",
    });

    WorkspaceSessionState workspace;
    workspace.id = 7;
    workspace.name = "session";
    workspace.name_user_set = true;
    workspace.host_manager = std::move(host_manager_state);

    AppSessionState state;
    state.session_id = "workbench";
    state.session_name = "workbench";
    state.active_workspace_id = 7;
    state.next_workspace_id = 8;
    state.workspaces.push_back(std::move(workspace));

    std::string save_error;
    REQUIRE(save_session_state(state, &save_error));
    REQUIRE(save_error.empty());

    std::string load_error;
    auto loaded = load_session_state("workbench", &load_error);
    REQUIRE(loaded);
    REQUIRE(load_error.empty());
    REQUIRE(loaded->session_id == "workbench");
    REQUIRE(loaded->session_name == "workbench");
    REQUIRE(loaded->active_workspace_id == 7);
    REQUIRE(loaded->next_workspace_id == 8);
    REQUIRE(loaded->workspaces.size() == 1);

    const WorkspaceSessionState& loaded_workspace = loaded->workspaces.front();
    CHECK(loaded_workspace.id == 7);
    CHECK(loaded_workspace.name == "session");
    CHECK(loaded_workspace.name_user_set);
    REQUIRE(loaded_workspace.host_manager.panes.size() == 2);
    CHECK(loaded_workspace.host_manager.zoomed);
    CHECK(loaded_workspace.host_manager.zoomed_leaf == right);

    SplitTree restored_tree;
    REQUIRE(restored_tree.restore(loaded_workspace.host_manager.tree, 1200, 800));
    CHECK(restored_tree.leaf_count() == 2);
    CHECK(restored_tree.focused() == right);
    CHECK(restored_tree.descriptor_for(left).pixel_size.x == tree.descriptor_for(left).pixel_size.x);
    CHECK(restored_tree.descriptor_for(right).pixel_pos.x == tree.descriptor_for(right).pixel_pos.x);

    CHECK(loaded_workspace.host_manager.panes[0].pane_name == "left");
    CHECK(loaded_workspace.host_manager.panes[0].pane_id == "pane-left");
    CHECK(loaded_workspace.host_manager.panes[0].launch.working_dir == "D:/left");
    CHECK(loaded_workspace.host_manager.panes[1].pane_name == "right");
    CHECK(loaded_workspace.host_manager.panes[1].pane_id == "pane-right");
    CHECK(loaded_workspace.host_manager.panes[1].launch.args == (std::vector<std::string>{ "-NoProfile" }));

    const auto sessions = list_saved_sessions(&load_error);
    REQUIRE(load_error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "workbench");
    CHECK(sessions[0].session_name == "workbench");
    CHECK(sessions[0].workspace_count == 1);
    CHECK(sessions[0].pane_count == 2);
}

TEST_CASE("session state: distinct session ids persist separately", "[session_state]")
{
    TempDir temp_dir("session-state-separate");
    HomeDirRedirect redirect(temp_dir.path);

    auto make_workspace = [](int id, std::string name) {
        SplitTree tree;
        const LeafId leaf = tree.reset(800, 600);
        WorkspaceSessionState workspace;
        workspace.id = id;
        workspace.name = std::move(name);
        workspace.name_user_set = true;
        workspace.host_manager.tree = tree.snapshot();
        workspace.host_manager.panes.push_back({
            .leaf_id = leaf,
            .launch = {
                .kind = HostKind::PowerShell,
                .command = "pwsh",
                .args = {},
                .working_dir = "D:/tmp",
                .source_path = "",
                .startup_commands = {},
            },
            .pane_name = "shell",
        });
        return workspace;
    };

    AppSessionState alpha;
    alpha.session_id = "alpha";
    alpha.session_name = "Alpha Session";
    alpha.active_workspace_id = 1;
    alpha.next_workspace_id = 2;
    alpha.workspaces.push_back(make_workspace(1, "alpha"));

    AppSessionState beta;
    beta.session_id = "beta/dev";
    beta.session_name = "beta/dev";
    beta.active_workspace_id = 2;
    beta.next_workspace_id = 3;
    beta.workspaces.push_back(make_workspace(2, "beta"));

    std::string error;
    REQUIRE(save_session_state(alpha, &error));
    REQUIRE(error.empty());
    REQUIRE(save_session_state(beta, &error));
    REQUIRE(error.empty());

    const auto sessions = list_saved_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 2);
    CHECK(sessions[0].session_id == "alpha");
    CHECK(sessions[0].session_name == "Alpha Session");
    CHECK(sessions[1].session_id == "beta/dev");
}

TEST_CASE("session state: delete removes saved session state", "[session_state]")
{
    TempDir temp_dir("session-state-delete");
    HomeDirRedirect redirect(temp_dir.path);

    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    AppSessionState state;
    state.session_id = "delete-me";
    state.session_name = "delete-me";
    state.active_workspace_id = 1;
    state.next_workspace_id = 2;

    WorkspaceSessionState workspace;
    workspace.id = 1;
    workspace.name = "delete-me";
    workspace.name_user_set = true;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/tmp",
        },
        .pane_name = "shell",
    });
    state.workspaces.push_back(std::move(workspace));

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(session_state_path("delete-me")));

    REQUIRE(delete_session_state("delete-me", &error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(std::filesystem::exists(session_state_path("delete-me")));
    REQUIRE_FALSE(load_session_state("delete-me", &error).has_value());
}

TEST_CASE("session state: metadata-only live session appears in listings", "[session_state]")
{
    TempDir temp_dir("session-state-metadata-only");
    HomeDirRedirect redirect(temp_dir.path);

    SessionRuntimeMetadata metadata;
    metadata.session_id = "live-only";
    metadata.session_name = "Live Only";
    metadata.live = true;
    metadata.detached = true;
    metadata.owner_pid = 4242;
    metadata.last_attached_unix_s = 111;
    metadata.last_detached_unix_s = 222;

    std::string error;
    REQUIRE(save_session_runtime_metadata(metadata, &error));
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(session_metadata_path("live-only")));

    auto loaded = load_session_runtime_metadata("live-only", &error);
    REQUIRE(loaded);
    REQUIRE(error.empty());
    CHECK(loaded->session_name == "Live Only");
    CHECK(loaded->live);
    CHECK(loaded->detached);
    CHECK(loaded->owner_pid == 4242);

    const auto sessions = list_saved_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "live-only");
    CHECK(sessions[0].session_name == "Live Only");
    CHECK(sessions[0].live);
    CHECK(sessions[0].detached);
    CHECK_FALSE(sessions[0].has_saved_state);
    CHECK(sessions[0].owner_pid == 4242);
    CHECK(sessions[0].pane_count == 0);
    CHECK_FALSE(has_saved_session_state("live-only", &error));
    REQUIRE(error.empty());
}

TEST_CASE("session state: clearing runtime liveness preserves session metadata history", "[session_state]")
{
    TempDir temp_dir("session-state-clear-liveness");
    HomeDirRedirect redirect(temp_dir.path);

    SessionRuntimeMetadata metadata;
    metadata.session_id = "clear-me";
    metadata.session_name = "Clear Me";
    metadata.live = true;
    metadata.detached = true;
    metadata.owner_pid = 4242;
    metadata.last_attached_unix_s = 111;
    metadata.last_detached_unix_s = 222;

    std::string error;
    REQUIRE(save_session_runtime_metadata(metadata, &error));
    REQUIRE(error.empty());

    REQUIRE(clear_session_runtime_liveness("clear-me", &error));
    REQUIRE(error.empty());

    auto loaded = load_session_runtime_metadata("clear-me", &error);
    REQUIRE(loaded);
    REQUIRE(error.empty());
    CHECK(loaded->session_name == "Clear Me");
    CHECK_FALSE(loaded->live);
    CHECK_FALSE(loaded->detached);
    CHECK(loaded->owner_pid == 0);
    CHECK(loaded->last_attached_unix_s == 111);
    CHECK(loaded->last_detached_unix_s == 222);
}

TEST_CASE("session state: metadata merges into saved session summary", "[session_state]")
{
    TempDir temp_dir("session-state-metadata-merge");
    HomeDirRedirect redirect(temp_dir.path);

    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    AppSessionState state;
    state.session_id = "merge-me";
    state.session_name = "merge-me";
    state.active_workspace_id = 1;
    state.next_workspace_id = 2;

    WorkspaceSessionState workspace;
    workspace.id = 1;
    workspace.name = "merge-me";
    workspace.name_user_set = true;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/tmp",
        },
        .pane_name = "shell",
    });
    state.workspaces.push_back(std::move(workspace));

    SessionRuntimeMetadata metadata;
    metadata.session_id = "merge-me";
    metadata.session_name = "Merge Session";
    metadata.live = true;
    metadata.detached = false;
    metadata.owner_pid = 999;
    metadata.last_attached_unix_s = 123;

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());
    REQUIRE(save_session_runtime_metadata(metadata, &error));
    REQUIRE(error.empty());

    const auto sessions = list_saved_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "merge-me");
    CHECK(sessions[0].session_name == "Merge Session");
    CHECK(sessions[0].has_saved_state);
    CHECK(sessions[0].live);
    CHECK_FALSE(sessions[0].detached);
    CHECK(sessions[0].owner_pid == 999);
    CHECK(sessions[0].pane_count == 1);
}

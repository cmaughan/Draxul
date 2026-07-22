#include "session_id.h"
#include "session_state.h"
#include "split_tree.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>

#include <cctype>
#include <fstream>
#include <iterator>

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

    SessionSnapshot state;
    state.session_id = base;
    state.session_name = "Work Bench";
    state.active_tab_id = 1;
    state.next_tab_id = 2;

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());

    auto generated = make_unique_session_id("Work Bench", fixed_time);
    REQUIRE(generated);
    CHECK(*generated == base + "-2");
}

TEST_CASE("session state: save/load round-trip preserves tab topology", "[session_state]")
{
    TempDir temp_dir("session-state-roundtrip");
    HomeDirRedirect redirect(temp_dir.path);

    SplitTree tree;
    const LeafId left = tree.reset(1200, 800);
    const LeafId right = tree.split_leaf(left, SplitDirection::Vertical);
    tree.set_focused(right);

    PaneManager::PaneLayoutSnapshot pane_manager_state;
    pane_manager_state.tree = tree.snapshot();
    pane_manager_state.zoomed = true;
    pane_manager_state.zoomed_leaf = right;
    pane_manager_state.panes.push_back({
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
    pane_manager_state.panes.push_back({
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

    TabSnapshot tab;
    tab.id = 7;
    tab.name = "session";
    tab.name_user_set = true;
    tab.pane_manager = std::move(pane_manager_state);

    SessionSnapshot state;
    state.session_id = "workbench";
    state.session_name = "workbench";
    state.active_tab_id = 7;
    state.next_tab_id = 8;
    state.tabs.push_back(std::move(tab));

    std::string save_error;
    REQUIRE(save_session_state(state, &save_error));
    REQUIRE(save_error.empty());

    std::ifstream saved_file(session_state_path("workbench"));
    REQUIRE(saved_file.is_open());
    const std::string saved_text{
        std::istreambuf_iterator<char>(saved_file), std::istreambuf_iterator<char>()
    };
    CHECK(saved_text.find("active_workspace_id") != std::string::npos);
    CHECK(saved_text.find("next_workspace_id") != std::string::npos);
    CHECK(saved_text.find("workspaces") != std::string::npos);
    CHECK(saved_text.find("host_manager") != std::string::npos);
    CHECK(saved_text.find("active_tab_id") == std::string::npos);
    CHECK(saved_text.find("pane_manager") == std::string::npos);

    std::string load_error;
    auto loaded = load_session_state("workbench", &load_error);
    REQUIRE(loaded);
    REQUIRE(load_error.empty());
    REQUIRE(loaded->session_id == "workbench");
    REQUIRE(loaded->session_name == "workbench");
    REQUIRE(loaded->active_tab_id == 7);
    REQUIRE(loaded->next_tab_id == 8);
    REQUIRE(loaded->tabs.size() == 1);

    const TabSnapshot& loaded_tab = loaded->tabs.front();
    CHECK(loaded_tab.id == 7);
    CHECK(loaded_tab.name == "session");
    CHECK(loaded_tab.name_user_set);
    REQUIRE(loaded_tab.pane_manager.panes.size() == 2);
    CHECK(loaded_tab.pane_manager.zoomed);
    CHECK(loaded_tab.pane_manager.zoomed_leaf == right);

    SplitTree restored_tree;
    REQUIRE(restored_tree.restore(loaded_tab.pane_manager.tree, 1200, 800));
    CHECK(restored_tree.leaf_count() == 2);
    CHECK(restored_tree.focused() == right);
    CHECK(restored_tree.descriptor_for(left).pixel_size.x == tree.descriptor_for(left).pixel_size.x);
    CHECK(restored_tree.descriptor_for(right).pixel_pos.x == tree.descriptor_for(right).pixel_pos.x);

    CHECK(loaded_tab.pane_manager.panes[0].pane_name == "left");
    CHECK(loaded_tab.pane_manager.panes[0].pane_id == "pane-left");
    CHECK(loaded_tab.pane_manager.panes[0].launch.working_dir == "D:/left");
    CHECK(loaded_tab.pane_manager.panes[1].pane_name == "right");
    CHECK(loaded_tab.pane_manager.panes[1].pane_id == "pane-right");
    CHECK(loaded_tab.pane_manager.panes[1].launch.args == (std::vector<std::string>{ "-NoProfile" }));

    const auto sessions = list_saved_sessions(&load_error);
    REQUIRE(load_error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "workbench");
    CHECK(sessions[0].session_name == "workbench");
    CHECK(sessions[0].tab_count == 1);
    CHECK(sessions[0].pane_count == 2);
}

TEST_CASE("session state: distinct session ids persist separately", "[session_state]")
{
    TempDir temp_dir("session-state-separate");
    HomeDirRedirect redirect(temp_dir.path);

    auto make_tab = [](int id, std::string name) {
        SplitTree tree;
        const LeafId leaf = tree.reset(800, 600);
        TabSnapshot tab;
        tab.id = id;
        tab.name = std::move(name);
        tab.name_user_set = true;
        tab.pane_manager.tree = tree.snapshot();
        tab.pane_manager.panes.push_back({
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
        return tab;
    };

    SessionSnapshot alpha;
    alpha.session_id = "alpha";
    alpha.session_name = "Alpha Session";
    alpha.active_tab_id = 1;
    alpha.next_tab_id = 2;
    alpha.tabs.push_back(make_tab(1, "alpha"));

    SessionSnapshot beta;
    beta.session_id = "beta/dev";
    beta.session_name = "beta/dev";
    beta.active_tab_id = 2;
    beta.next_tab_id = 3;
    beta.tabs.push_back(make_tab(2, "beta"));

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

    SessionSnapshot state;
    state.session_id = "delete-me";
    state.session_name = "delete-me";
    state.active_tab_id = 1;
    state.next_tab_id = 2;

    TabSnapshot tab;
    tab.id = 1;
    tab.name = "delete-me";
    tab.name_user_set = true;
    tab.pane_manager.tree = tree.snapshot();
    tab.pane_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/tmp",
        },
        .pane_name = "shell",
    });
    state.tabs.push_back(std::move(tab));

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(session_state_path("delete-me")));

    REQUIRE(delete_session_state("delete-me", &error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(std::filesystem::exists(session_state_path("delete-me")));
    REQUIRE_FALSE(load_session_state("delete-me", &error).has_value());
}

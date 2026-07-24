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

namespace
{

std::string read_session_fixture(std::string_view name)
{
    const std::filesystem::path path = std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "tests" / "fixtures" / "session-state" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    return {
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()
    };
}

SessionSnapshot make_single_pane_session_snapshot()
{
    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    TabSnapshot tab;
    tab.id = 1;
    tab.name = "shell";
    tab.pane_layout.tree = tree.snapshot();
    tab.pane_layout.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/work",
        },
        .pane_name = "shell",
        .pane_id = "pane-1",
    });

    SpaceSnapshot space;
    space.id = 1;
    space.name = "work";
    space.root_directory = "D:/work";
    space.active_tab_id = 1;
    space.next_tab_id = 2;
    space.tabs.push_back(std::move(tab));

    SessionSnapshot state;
    state.session_id = "bounded";
    state.session_name = "Bounded";
    state.active_space_id = 1;
    state.next_space_id = 2;
    state.spaces.push_back(std::move(space));
    return state;
}

std::unique_ptr<SplitTree::SnapshotNode> make_deep_snapshot_tree(
    size_t split_depth, LeafId& next_leaf)
{
    auto node = std::make_unique<SplitTree::SnapshotNode>();
    if (split_depth == 0)
    {
        node->leaf_id = next_leaf++;
        return node;
    }

    node->is_leaf = false;
    node->direction = SplitDirection::Vertical;
    node->ratio = 0.5f;
    node->first = make_deep_snapshot_tree(split_depth - 1, next_leaf);
    node->second = std::make_unique<SplitTree::SnapshotNode>();
    node->second->leaf_id = next_leaf++;
    return node;
}

} // namespace

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

    PaneManager::PaneLayoutSnapshot pane_layout;
    pane_layout.tree = tree.snapshot();
    pane_layout.zoomed = true;
    pane_layout.zoomed_leaf = right;
    pane_layout.panes.push_back({
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
    pane_layout.panes.push_back({
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
        .agent = AgentIdentity{
            .profile_id = "codex",
            .kind = "codex",
            .display_name = "Codex",
            .instance_id = "agent-4-7-pane-right",
        },
        .agent_session = AgentSessionRef{
            .source = "draxul:codex",
            .agent_kind = "codex",
            .integration_version = 1,
            .sequence = 42,
            .kind = AgentSessionRefKind::Id,
            .value = "codex-session-42",
        },
        .restore_policy = AgentRestorePolicy::ResumeIfAvailable,
    });

    TabSnapshot tab;
    tab.id = 7;
    tab.name = "session";
    tab.name_user_set = true;
    tab.pane_layout = std::move(pane_layout);

    SessionSnapshot state;
    state.session_id = "workbench";
    state.session_name = "workbench";
    state.active_space_id = 11;
    state.next_space_id = 12;

    SpaceSnapshot first_space;
    first_space.id = 4;
    first_space.name = "Draxul";
    first_space.root_directory = "D:/dev/Draxul";
    first_space.active_tab_id = 7;
    first_space.next_tab_id = 8;
    first_space.tabs.push_back(std::move(tab));
    state.spaces.push_back(std::move(first_space));

    SplitTree second_tree;
    const LeafId second_leaf = second_tree.reset(1200, 800);
    TabSnapshot second_tab;
    second_tab.id = 22;
    second_tab.name = "tokens";
    second_tab.name_user_set = true;
    second_tab.pane_layout.tree = second_tree.snapshot();
    second_tab.pane_layout.panes.push_back({
        .leaf_id = second_leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/dev/tokenfu",
        },
        .pane_name = "tokenfu",
        .pane_id = "tokenfu-pane",
    });
    SpaceSnapshot second_space;
    second_space.id = 11;
    second_space.name = "TokenFu";
    second_space.root_directory = "D:/dev/tokenfu";
    second_space.active_tab_id = 22;
    second_space.next_tab_id = 30;
    second_space.tabs.push_back(std::move(second_tab));
    state.spaces.push_back(std::move(second_space));

    std::string save_error;
    REQUIRE(save_session_state(state, &save_error));
    REQUIRE(save_error.empty());

    std::ifstream saved_file(session_state_path("workbench"));
    REQUIRE(saved_file.is_open());
    const std::string saved_text{
        std::istreambuf_iterator<char>(saved_file), std::istreambuf_iterator<char>()
    };
    CHECK(saved_text.find("version = 3") != std::string::npos);
    CHECK(saved_text.find("active_space_id") != std::string::npos);
    CHECK(saved_text.find("next_space_id") != std::string::npos);
    CHECK(saved_text.find("[[spaces]]") != std::string::npos);
    CHECK(saved_text.find("[[spaces.tabs]]") != std::string::npos);
    CHECK(saved_text.find("[spaces.tabs.pane_layout]") != std::string::npos);
    CHECK(saved_text.find("instance_id = 'agent-4-7-pane-right'")
        != std::string::npos);
    CHECK(saved_text.find("value = 'codex-session-42'")
        != std::string::npos);
    CHECK(saved_text.find("running =") == std::string::npos);
    CHECK(saved_text.find("active_workspace_id") == std::string::npos);
    CHECK(saved_text.find("host_manager") == std::string::npos);

    std::string load_error;
    auto loaded = load_session_state("workbench", &load_error);
    REQUIRE(loaded);
    REQUIRE(load_error.empty());
    REQUIRE(loaded->session_id == "workbench");
    REQUIRE(loaded->session_name == "workbench");
    REQUIRE(loaded->version == 3);
    REQUIRE(loaded->active_space_id == 11);
    REQUIRE(loaded->next_space_id == 12);
    REQUIRE(loaded->spaces.size() == 2);

    const SpaceSnapshot& loaded_first_space = loaded->spaces[0];
    CHECK(loaded_first_space.id == 4);
    CHECK(loaded_first_space.name == "Draxul");
    CHECK(loaded_first_space.root_directory == std::filesystem::path("D:/dev/Draxul"));
    CHECK(loaded_first_space.active_tab_id == 7);
    CHECK(loaded_first_space.next_tab_id == 8);
    REQUIRE(loaded_first_space.tabs.size() == 1);
    const TabSnapshot& loaded_tab = loaded_first_space.tabs.front();
    CHECK(loaded_tab.id == 7);
    CHECK(loaded_tab.name == "session");
    CHECK(loaded_tab.name_user_set);
    REQUIRE(loaded_tab.pane_layout.panes.size() == 2);
    CHECK(loaded_tab.pane_layout.zoomed);
    CHECK(loaded_tab.pane_layout.zoomed_leaf == right);

    SplitTree restored_tree;
    REQUIRE(restored_tree.restore(loaded_tab.pane_layout.tree, 1200, 800));
    CHECK(restored_tree.leaf_count() == 2);
    CHECK(restored_tree.focused() == right);
    CHECK(restored_tree.descriptor_for(left).pixel_size.x == tree.descriptor_for(left).pixel_size.x);
    CHECK(restored_tree.descriptor_for(right).pixel_pos.x == tree.descriptor_for(right).pixel_pos.x);

    CHECK(loaded_tab.pane_layout.panes[0].pane_name == "left");
    CHECK(loaded_tab.pane_layout.panes[0].pane_id == "pane-left");
    CHECK(loaded_tab.pane_layout.panes[0].launch.working_dir == "D:/left");
    CHECK(loaded_tab.pane_layout.panes[1].pane_name == "right");
    CHECK(loaded_tab.pane_layout.panes[1].pane_id == "pane-right");
    CHECK(loaded_tab.pane_layout.panes[1].launch.args == (std::vector<std::string>{ "-NoProfile" }));
    REQUIRE(loaded_tab.pane_layout.panes[1].agent);
    CHECK(loaded_tab.pane_layout.panes[1].agent->kind == "codex");
    CHECK(loaded_tab.pane_layout.panes[1].agent->display_name == "Codex");
    CHECK(loaded_tab.pane_layout.panes[1].agent->instance_id
        == "agent-4-7-pane-right");
    REQUIRE(loaded_tab.pane_layout.panes[1].agent_session);
    CHECK(loaded_tab.pane_layout.panes[1].agent_session->source
        == "draxul:codex");
    CHECK(loaded_tab.pane_layout.panes[1].agent_session->value
        == "codex-session-42");

    const SpaceSnapshot& loaded_second_space = loaded->spaces[1];
    CHECK(loaded_second_space.id == 11);
    CHECK(loaded_second_space.name == "TokenFu");
    CHECK(loaded_second_space.root_directory == std::filesystem::path("D:/dev/tokenfu"));
    CHECK(loaded_second_space.active_tab_id == 22);
    CHECK(loaded_second_space.next_tab_id == 30);
    REQUIRE(loaded_second_space.tabs.size() == 1);
    CHECK(loaded_second_space.tabs[0].id == 22);
    CHECK(loaded_second_space.tabs[0].pane_layout.panes[0].pane_id == "tokenfu-pane");

    const auto sessions = list_saved_sessions(&load_error);
    REQUIRE(load_error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "workbench");
    CHECK(sessions[0].session_name == "workbench");
    CHECK(sessions[0].space_count == 2);
    CHECK(sessions[0].tab_count == 2);
    CHECK(sessions[0].pane_count == 3);
}

TEST_CASE("session state: historical v1 fixture decodes through pure codec",
    "[session_state][fixture]")
{
    std::string error;
    auto decoded = decode_session_state(
        read_session_fixture("v1-historical-valid.toml"), &error);

    REQUIRE(decoded);
    REQUIRE(error.empty());
    CHECK(decoded->version == 3);
    CHECK(decoded->session_id == "historical");
    CHECK(decoded->session_name == "Historical Session");
    CHECK(decoded->active_space_id == kDefaultSpaceId);
    CHECK(decoded->next_space_id == kDefaultSpaceId + 1);
    REQUIRE(decoded->spaces.size() == 1);
    const SpaceSnapshot& migrated_space = decoded->spaces.front();
    CHECK(migrated_space.id == kDefaultSpaceId);
    CHECK(migrated_space.name == "default");
    CHECK(migrated_space.active_tab_id == 3);
    CHECK(migrated_space.next_tab_id == 4);
    REQUIRE(migrated_space.tabs.size() == 1);
    CHECK(migrated_space.tabs[0].id == 3);
    CHECK(migrated_space.tabs[0].name == "shells");
    CHECK(migrated_space.tabs[0].name_user_set);
    REQUIRE(migrated_space.tabs[0].pane_layout.panes.size() == 2);
    CHECK(migrated_space.tabs[0].pane_layout.panes[0].pane_id == "historical-left");
    CHECK(migrated_space.tabs[0].pane_layout.panes[1].pane_id == "historical-right");

    SplitTree restored_tree;
    REQUIRE(restored_tree.restore(migrated_space.tabs[0].pane_layout.tree, 1200, 800));
    CHECK(restored_tree.leaf_count() == 2);
    CHECK(restored_tree.focused() == 1);
}

TEST_CASE("session state: v2 snapshots migrate to the current in-memory model",
    "[session_state][migration]")
{
    SessionSnapshot state = make_single_pane_session_snapshot();
    std::string error;
    auto encoded = encode_session_state(state, &error);
    REQUIRE(encoded);
    REQUIRE(error.empty());
    const auto version = encoded->find("version = 3");
    REQUIRE(version != std::string::npos);
    encoded->replace(version, std::string("version = 3").size(), "version = 2");

    auto decoded = decode_session_state(*encoded, &error);
    REQUIRE(decoded);
    CHECK(error.empty());
    CHECK(decoded->version == 3);
    REQUIRE(decoded->spaces.size() == 1);
    REQUIRE(decoded->spaces[0].tabs.size() == 1);
    REQUIRE(decoded->spaces[0].tabs[0].pane_layout.panes.size() == 1);
    CHECK_FALSE(decoded->spaces[0].tabs[0].pane_layout.panes[0].agent_session);
}

TEST_CASE("session state: malformed and unsupported fixtures fail before file I/O",
    "[session_state][fixture]")
{
    std::string error;
    CHECK_FALSE(decode_session_state(
        read_session_fixture("v1-corrupt.toml"), &error));
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(decode_session_state(
        read_session_fixture("v1-unsupported-version.toml"), &error));
    CHECK(error == "Unsupported session state version.");
}

TEST_CASE("session state: duplicate stable ids are rejected by value validation",
    "[session_state][fixture]")
{
    std::string error;
    CHECK_FALSE(decode_session_state(
        read_session_fixture("v1-duplicate-tab-id.toml"), &error));
    CHECK(error == "Session state contains a duplicate tab id.");
}

TEST_CASE("session state: v2 rejects duplicate Space identities",
    "[session_state]")
{
    SessionSnapshot state;
    state.active_space_id = 5;
    state.next_space_id = 6;
    state.spaces.push_back(SpaceSnapshot{ .id = 5, .name = "first" });
    state.spaces.push_back(SpaceSnapshot{ .id = 5, .name = "second" });

    std::string error;
    CHECK_FALSE(validate_session_snapshot(state, &error));
    CHECK(error == "Session state contains a duplicate or invalid Space id.");
    CHECK_FALSE(encode_session_state(state, &error));
}

TEST_CASE("session state: a native agent session has one owning pane",
    "[session_state][agent]")
{
    SessionSnapshot state = make_single_pane_session_snapshot();
    auto& first_pane = state.spaces[0].tabs[0].pane_layout.panes[0];
    first_pane.agent = AgentIdentity{
        .profile_id = "codex",
        .kind = "codex",
        .display_name = "Codex",
        .instance_id = "agent-one",
    };
    first_pane.agent_session = AgentSessionRef{
        .source = "draxul:codex",
        .agent_kind = "codex",
        .integration_version = 1,
        .sequence = 1,
        .kind = AgentSessionRefKind::Id,
        .value = "shared-native-session",
    };

    SessionSnapshot duplicate_state = make_single_pane_session_snapshot();
    SpaceSnapshot duplicate = std::move(duplicate_state.spaces[0]);
    duplicate.id = 2;
    duplicate.name = "duplicate";
    duplicate.tabs[0].id = 2;
    auto& second_pane = duplicate.tabs[0].pane_layout.panes[0];
    second_pane.pane_id = "pane-2";
    second_pane.agent = AgentIdentity{
        .profile_id = "codex",
        .kind = "codex",
        .display_name = "Codex",
        .instance_id = "agent-two",
    };
    second_pane.agent_session = AgentSessionRef{
        .source = "draxul:codex",
        .agent_kind = "codex",
        .integration_version = 1,
        .sequence = 2,
        .kind = AgentSessionRefKind::Id,
        .value = "shared-native-session",
    };
    state.spaces.push_back(std::move(duplicate));
    state.next_space_id = 3;

    std::string error;
    CHECK_FALSE(validate_session_snapshot(state, &error));
    CHECK(error == "Session state contains a duplicate native agent session.");
}

TEST_CASE("session state: recovery input size and cardinality are bounded",
    "[session_state][hardening]")
{
    TempDir temp_dir("session-state-bounds");
    HomeDirRedirect redirect(temp_dir.path);

    std::string error;
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');
    CHECK_FALSE(decode_session_state(oversized, &error));
    CHECK(error == "Session state exceeds the file size limit.");

    const std::filesystem::path oversized_path = session_state_path("oversized");
    REQUIRE(std::filesystem::create_directories(oversized_path.parent_path()));
    std::ofstream oversized_file(oversized_path, std::ios::binary);
    REQUIRE(oversized_file.is_open());
    oversized_file << oversized;
    oversized_file.close();
    CHECK_FALSE(load_session_state("oversized", &error));
    CHECK(error == "Session state exceeds the file size limit.");

    SessionSnapshot too_many_spaces;
    for (SpaceId id = 1; id <= 65; ++id)
        too_many_spaces.spaces.push_back(SpaceSnapshot{ .id = id });
    CHECK_FALSE(validate_session_snapshot(too_many_spaces, &error));
    CHECK(error == "Session state exceeds the Space limit.");

    SessionSnapshot too_deep = make_single_pane_session_snapshot();
    LeafId next_leaf = 0;
    too_deep.spaces[0].tabs[0].pane_layout.tree.root =
        make_deep_snapshot_tree(65, next_leaf);
    too_deep.spaces[0].tabs[0].pane_layout.tree.next_leaf_id = next_leaf;
    CHECK_FALSE(validate_session_snapshot(too_deep, &error));
    CHECK(error == "Session state layout exceeds structural limits.");
}

TEST_CASE("session state: diagnostics do not expose commands or paths",
    "[session_state][hardening]")
{
    constexpr std::string_view secret = "SUPER_SECRET_SESSION_VALUE";
    std::string error;

    SessionSnapshot command_state = make_single_pane_session_snapshot();
    command_state.spaces[0].tabs[0].pane_layout.panes[0].launch.command =
        std::string(secret) + std::string(8192, 'x');
    CHECK_FALSE(validate_session_snapshot(command_state, &error));
    CHECK(error == "Session state host command exceeds the text limit.");
    CHECK(error.find(secret) == std::string::npos);

    SessionSnapshot path_state = make_single_pane_session_snapshot();
    path_state.spaces[0].root_directory =
        std::string(secret) + std::string(8192, 'x');
    CHECK_FALSE(validate_session_snapshot(path_state, &error));
    CHECK(error == "Session state root directory exceeds the text limit.");
    CHECK(error.find(secret) == std::string::npos);

    const std::string malformed =
        "version = 2\ncommand = \"" + std::string(secret) + "\n";
    CHECK_FALSE(decode_session_state(malformed, &error));
    CHECK(error == "Session state TOML could not be parsed.");
    CHECK(error.find(secret) == std::string::npos);
}

TEST_CASE("session state: filesystem availability and host restorability are not codec concerns",
    "[session_state][fixture]")
{
    std::string error;
    auto missing_directory = decode_session_state(
        read_session_fixture("v1-missing-directory.toml"), &error);
    REQUIRE(missing_directory);
    REQUIRE(error.empty());
    REQUIRE(missing_directory->spaces.size() == 1);
    REQUIRE(missing_directory->spaces[0].tabs.size() == 1);
    REQUIRE(missing_directory->spaces[0].tabs[0].pane_layout.panes.size() == 1);
    CHECK(missing_directory->spaces[0].tabs[0].pane_layout.panes[0].launch.working_dir
        == "Z:/draxul-fixture/path-that-does-not-exist");

    auto non_restorable = decode_session_state(
        read_session_fixture("v1-non-restorable-host.toml"), &error);
    REQUIRE(non_restorable);
    REQUIRE(error.empty());
    REQUIRE(non_restorable->spaces.size() == 1);
    REQUIRE(non_restorable->spaces[0].tabs.size() == 1);
    REQUIRE(non_restorable->spaces[0].tabs[0].pane_layout.panes.size() == 1);
    CHECK(non_restorable->spaces[0].tabs[0].pane_layout.panes[0].launch.kind
        == HostKind::Markdown);
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
        tab.pane_layout.tree = tree.snapshot();
        tab.pane_layout.panes.push_back({
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
    alpha.active_space_id = 1;
    alpha.next_space_id = 2;
    SpaceSnapshot alpha_space;
    alpha_space.id = 1;
    alpha_space.active_tab_id = 1;
    alpha_space.next_tab_id = 2;
    alpha_space.tabs.push_back(make_tab(1, "alpha"));
    alpha.spaces.push_back(std::move(alpha_space));

    SessionSnapshot beta;
    beta.session_id = "beta/dev";
    beta.session_name = "beta/dev";
    beta.active_space_id = 2;
    beta.next_space_id = 3;
    SpaceSnapshot beta_space;
    beta_space.id = 2;
    beta_space.active_tab_id = 2;
    beta_space.next_tab_id = 3;
    beta_space.tabs.push_back(make_tab(2, "beta"));
    beta.spaces.push_back(std::move(beta_space));

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

TEST_CASE("session state: failed temporary write preserves the last good snapshot",
    "[session_state][filesystem]")
{
    TempDir temp_dir("session-state-replace-safe");
    const std::filesystem::path path = temp_dir.path / "session.toml";

    SessionSnapshot original;
    original.session_id = "replace-safe";
    original.session_name = "Original";
    std::string error;
    REQUIRE(save_session_state_to_path(original, path, &error));
    REQUIRE(error.empty());

    std::ifstream original_file(path, std::ios::binary);
    REQUIRE(original_file.is_open());
    const std::string original_text{
        std::istreambuf_iterator<char>(original_file), std::istreambuf_iterator<char>()
    };
    original_file.close();

    std::filesystem::path blocked_temporary = path;
    blocked_temporary += ".tmp";
    REQUIRE(std::filesystem::create_directory(blocked_temporary));

    SessionSnapshot replacement;
    replacement.session_id = "replace-safe";
    replacement.session_name = "Replacement";
    CHECK_FALSE(save_session_state_to_path(replacement, path, &error));
    CHECK_FALSE(error.empty());

    std::ifstream preserved_file(path, std::ios::binary);
    REQUIRE(preserved_file.is_open());
    const std::string preserved_text{
        std::istreambuf_iterator<char>(preserved_file), std::istreambuf_iterator<char>()
    };
    CHECK(preserved_text == original_text);

    auto decoded = decode_session_state(preserved_text, &error);
    REQUIRE(decoded);
    CHECK(decoded->session_name == "Original");
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
    state.active_space_id = 1;
    state.next_space_id = 2;

    TabSnapshot tab;
    tab.id = 1;
    tab.name = "delete-me";
    tab.name_user_set = true;
    tab.pane_layout.tree = tree.snapshot();
    tab.pane_layout.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/tmp",
        },
        .pane_name = "shell",
    });
    SpaceSnapshot space;
    space.id = 1;
    space.active_tab_id = 1;
    space.next_tab_id = 2;
    space.tabs.push_back(std::move(tab));
    state.spaces.push_back(std::move(space));

    std::string error;
    REQUIRE(save_session_state(state, &error));
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(session_state_path("delete-me")));

    REQUIRE(delete_session_state("delete-me", &error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(std::filesystem::exists(session_state_path("delete-me")));
    REQUIRE_FALSE(load_session_state("delete-me", &error).has_value());
}

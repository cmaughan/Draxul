// WI 128 — Inline tab rename state machine.
//
// These tests exercise ChromeHost's rename API directly without standing up
// a renderer or text service. The methods under test only mutate the
// internal TabEditState and a small set of std::function callbacks, so we
// can construct ChromeHost with a near-empty Deps.

#include <catch2/catch_all.hpp>

#include "chrome_host.h"
#include "pane_manager.h"
#include "tab.h"

#include <SDL3/SDL_keycode.h>
#include <memory>
#include <vector>

using namespace draxul;

namespace
{
std::unique_ptr<Tab> make_test_tab(int id, std::string name)
{
    auto tab = std::make_unique<Tab>(id, PaneManager::Deps{});
    tab->name = std::move(name);
    return tab;
}

struct RenameFixture
{
    std::vector<std::unique_ptr<Tab>> tabs;
    int active = 1;
    std::unique_ptr<ChromeHost> host;
    int frame_requests = 0;
    // Stand-in for PaneManager::pane_user_names_ — exercised by the pane
    // rename test cases without standing up a real PaneManager.
    std::unordered_map<LeafId, std::string> pane_names;

    RenameFixture()
    {
        tabs.push_back(make_test_tab(1, "alpha"));
        tabs.push_back(make_test_tab(2, "beta"));
        active = 1;

        ChromeHost::Deps deps;
        deps.tabs = &tabs;
        deps.active_tab_id = &active;
        deps.set_tab_name = [this](int tab_id, std::string name) {
            for (auto& tab : tabs)
            {
                if (tab->id == tab_id)
                {
                    tab->name = std::move(name);
                    tab->name_user_set = true;
                    return;
                }
            }
        };
        deps.set_pane_name = [this](LeafId leaf, std::string name) {
            if (name.empty())
                pane_names.erase(leaf);
            else
                pane_names[leaf] = std::move(name);
        };
        deps.get_pane_name = [this](LeafId leaf) -> std::string {
            auto it = pane_names.find(leaf);
            return it == pane_names.end() ? std::string{} : it->second;
        };
        deps.request_frame = [this]() { ++frame_requests; };
        host = std::make_unique<ChromeHost>(std::move(deps));
    }
};
} // namespace

TEST_CASE("ChromeHost rename: typing and Enter commits", "[chrome_host][rename]")
{
    RenameFixture f;

    f.host->begin_tab_rename(1);
    REQUIRE(f.host->is_editing_tab());
    REQUIRE(f.host->editing_tab_id() == 1);

    REQUIRE(f.host->on_rename_text_input("d"));
    REQUIRE(f.host->on_rename_text_input("e"));
    REQUIRE(f.host->on_rename_text_input("v"));

    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    REQUIRE_FALSE(f.host->is_editing_tab());

    // The seeded buffer was "alpha"; we appended "dev" → final name "alphadev".
    REQUIRE(f.tabs[0]->name == "alphadev");
    REQUIRE(f.tabs[0]->name_user_set);
    REQUIRE(f.tabs[1]->name == "beta");
    REQUIRE_FALSE(f.tabs[1]->name_user_set);
}

TEST_CASE("ChromeHost rename: Escape cancels without mutation", "[chrome_host][rename]")
{
    RenameFixture f;
    f.host->begin_tab_rename(2);
    f.host->on_rename_text_input("x");
    REQUIRE(f.host->on_rename_key(SDLK_ESCAPE));
    REQUIRE_FALSE(f.host->is_editing_tab());
    REQUIRE(f.tabs[1]->name == "beta");
    REQUIRE_FALSE(f.tabs[1]->name_user_set);
}

TEST_CASE("ChromeHost rename: Backspace deletes the previous codepoint", "[chrome_host][rename]")
{
    RenameFixture f;
    f.host->begin_tab_rename(1);
    // Cursor is at the end of "alpha".
    REQUIRE(f.host->on_rename_key(SDLK_BACKSPACE));
    REQUIRE(f.host->on_rename_key(SDLK_BACKSPACE));
    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    REQUIRE(f.tabs[0]->name == "alp");
}

TEST_CASE("ChromeHost rename: empty buffer commit keeps the original name", "[chrome_host][rename]")
{
    RenameFixture f;
    f.host->begin_tab_rename(1);
    for (int i = 0; i < 10; ++i)
        f.host->on_rename_key(SDLK_BACKSPACE);
    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    // Empty commit must NOT replace the name with "".
    REQUIRE(f.tabs[0]->name == "alpha");
    REQUIRE_FALSE(f.tabs[0]->name_user_set);
}

TEST_CASE("ChromeHost rename: cursor movement keys consumed even when buffer is empty",
    "[chrome_host][rename]")
{
    RenameFixture f;
    f.host->begin_tab_rename(1);
    REQUIRE(f.host->on_rename_key(SDLK_HOME));
    REQUIRE(f.host->on_rename_key(SDLK_END));
    REQUIRE(f.host->on_rename_key(SDLK_LEFT));
    REQUIRE(f.host->on_rename_key(SDLK_RIGHT));
    // Should still be editing — none of these end the session.
    REQUIRE(f.host->is_editing_tab());
}

TEST_CASE("ChromeHost rename: switching tabs commits the in-progress edit",
    "[chrome_host][rename]")
{
    RenameFixture f;
    f.host->begin_tab_rename(1);
    f.host->on_rename_text_input("X");
    f.host->begin_tab_rename(2);
    REQUIRE(f.host->is_editing_tab());
    REQUIRE(f.host->editing_tab_id() == 2);
    REQUIRE(f.tabs[0]->name == "alphaX");
    REQUIRE(f.tabs[0]->name_user_set);
}

TEST_CASE("ChromeHost rename: ignores keys when no session is active",
    "[chrome_host][rename]")
{
    RenameFixture f;
    REQUIRE_FALSE(f.host->on_rename_key(SDLK_RETURN));
    REQUIRE_FALSE(f.host->on_rename_text_input("a"));
    REQUIRE_FALSE(f.host->is_editing_tab());
}

TEST_CASE("ChromeHost pane rename: typing and Enter commits", "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 42;

    f.host->begin_pane_rename(kLeaf);
    REQUIRE(f.host->is_editing_pane());
    REQUIRE(f.host->is_editing());
    REQUIRE_FALSE(f.host->is_editing_tab());
    REQUIRE(f.host->editing_leaf_id() == kLeaf);

    REQUIRE(f.host->on_rename_text_input("z"));
    REQUIRE(f.host->on_rename_text_input("s"));
    REQUIRE(f.host->on_rename_text_input("h"));

    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    REQUIRE_FALSE(f.host->is_editing_pane());
    REQUIRE_FALSE(f.host->is_editing());

    REQUIRE(f.pane_names[kLeaf] == "zsh");
}

TEST_CASE("ChromeHost pane rename: get_pane_name seeds buffer", "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 7;
    f.pane_names[kLeaf] = "build";

    f.host->begin_pane_rename(kLeaf);
    REQUIRE(f.host->on_rename_text_input("X"));
    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    // Buffer should have been seeded with "build" then appended with "X".
    REQUIRE(f.pane_names[kLeaf] == "buildX");
}

TEST_CASE("ChromeHost pane rename: empty buffer commit clears override",
    "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 3;
    f.pane_names[kLeaf] = "old";

    f.host->begin_pane_rename(kLeaf);
    // Backspace through "old".
    for (int i = 0; i < 10; ++i)
        f.host->on_rename_key(SDLK_BACKSPACE);
    REQUIRE(f.host->on_rename_key(SDLK_RETURN));
    // Pane semantics differ from tab: an empty commit clears the override.
    REQUIRE(f.pane_names.find(kLeaf) == f.pane_names.end());
}

TEST_CASE("ChromeHost pane rename: Escape cancels without mutation",
    "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 9;
    f.pane_names[kLeaf] = "keep";

    f.host->begin_pane_rename(kLeaf);
    f.host->on_rename_text_input("Q");
    REQUIRE(f.host->on_rename_key(SDLK_ESCAPE));
    REQUIRE_FALSE(f.host->is_editing_pane());
    REQUIRE(f.pane_names[kLeaf] == "keep");
}

TEST_CASE("ChromeHost pane rename: switching from pane to tab edit commits in-progress",
    "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 11;

    f.host->begin_pane_rename(kLeaf);
    f.host->on_rename_text_input("p");
    f.host->on_rename_text_input("y");

    // Starting a tab rename must commit the in-progress pane edit.
    f.host->begin_tab_rename(1);
    REQUIRE(f.host->is_editing_tab());
    REQUIRE_FALSE(f.host->is_editing_pane());
    REQUIRE(f.pane_names[kLeaf] == "py");
}

TEST_CASE("ChromeHost pane rename: switching from tab to pane edit commits in-progress",
    "[chrome_host][rename][pane]")
{
    RenameFixture f;
    constexpr LeafId kLeaf = 12;

    f.host->begin_tab_rename(1);
    f.host->on_rename_text_input("Z");

    f.host->begin_pane_rename(kLeaf);
    REQUIRE(f.host->is_editing_pane());
    REQUIRE_FALSE(f.host->is_editing_tab());
    REQUIRE(f.tabs[0]->name == "alphaZ");
    REQUIRE(f.tabs[0]->name_user_set);
}

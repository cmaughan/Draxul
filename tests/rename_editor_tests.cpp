#include <catch2/catch_test_macros.hpp>

#include <draxul/rename_editor.h>

using namespace draxul;

TEST_CASE("RenameEditor commits UTF-8 text at codepoint boundaries", "[app_shell][rename]")
{
    RenameEditor editor;
    editor.begin_tab(7, "caf\xC3\xA9");

    CHECK(editor.handle_key(RenameKey::Left) == std::nullopt);
    CHECK(editor.insert("!"));
    const auto commit = editor.commit();

    REQUIRE(commit.has_value());
    CHECK(commit->target == RenameTarget::Tab);
    CHECK(commit->tab_id == 7);
    CHECK(commit->text == "caf!\xC3\xA9");
    CHECK_FALSE(editor.active());
}

TEST_CASE("RenameEditor cancel preserves no pending commit", "[app_shell][rename]")
{
    RenameEditor editor;
    editor.begin_pane(11, "build");
    REQUIRE(editor.insert("-2"));
    editor.cancel();

    CHECK_FALSE(editor.active());
    CHECK(editor.commit() == std::nullopt);
}

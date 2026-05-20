#include <catch2/catch_all.hpp>

#include <draxul/events.h>
#include <draxul/input_types.h>
#include <draxul/markdown/markdown_navigation.h>

#include <SDL3/SDL.h>

using namespace draxul;
using namespace draxul::markdown;

namespace
{

KeyEvent key_event(int keycode, ModifierFlags mod = kModNone, bool pressed = true)
{
    return KeyEvent{
        .scancode = 0,
        .keycode = keycode,
        .mod = mod,
        .pressed = pressed,
    };
}

} // namespace

TEST_CASE("markdown vim navigation maps line page and jump keys", "[markdown][navigation]")
{
    MarkdownNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_J)) == MarkdownNavigationCommand::LineDown);
    REQUIRE(navigation.on_key(key_event(SDLK_K)) == MarkdownNavigationCommand::LineUp);
    REQUIRE(navigation.on_key(key_event(SDLK_DOWN)) == MarkdownNavigationCommand::LineDown);
    REQUIRE(navigation.on_key(key_event(SDLK_UP)) == MarkdownNavigationCommand::LineUp);

    REQUIRE(navigation.on_key(key_event(SDLK_F, kModCtrl)) == MarkdownNavigationCommand::PageDown);
    REQUIRE(navigation.on_key(key_event(SDLK_B, kModCtrl)) == MarkdownNavigationCommand::PageUp);
    REQUIRE(navigation.on_key(key_event(SDLK_SPACE)) == MarkdownNavigationCommand::PageDown);
    REQUIRE(navigation.on_key(key_event(SDLK_PAGEUP)) == MarkdownNavigationCommand::PageUp);
    REQUIRE(navigation.on_key(key_event(SDLK_PAGEDOWN)) == MarkdownNavigationCommand::PageDown);

    REQUIRE(navigation.on_key(key_event(SDLK_HOME)) == MarkdownNavigationCommand::Top);
    REQUIRE(navigation.on_key(key_event(SDLK_END)) == MarkdownNavigationCommand::Bottom);
    REQUIRE(navigation.on_key(key_event(SDLK_G, kModShift)) == MarkdownNavigationCommand::Bottom);
}

TEST_CASE("markdown vim navigation recognizes gg and cancels stale g prefixes", "[markdown][navigation]")
{
    MarkdownNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_G)) == MarkdownNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_G)) == MarkdownNavigationCommand::Top);

    REQUIRE(navigation.on_key(key_event(SDLK_G)) == MarkdownNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_J)) == MarkdownNavigationCommand::LineDown);
    REQUIRE(navigation.on_key(key_event(SDLK_G)) == MarkdownNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_G, kModShift)) == MarkdownNavigationCommand::Bottom);
}

TEST_CASE("markdown vim navigation normalizes left and right modifier bits", "[markdown][navigation]")
{
    MarkdownNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_F, 0x0040)) == MarkdownNavigationCommand::PageDown);
    REQUIRE(navigation.on_key(key_event(SDLK_G, 0x0001)) == MarkdownNavigationCommand::Bottom);
}

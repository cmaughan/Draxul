#include <catch2/catch_all.hpp>

#include <draxul/events.h>
#include <draxul/input_types.h>
#include <draxul/kanban/kanban_navigation.h>

#include <SDL3/SDL.h>

using namespace draxul;
using namespace draxul::kanban;

namespace
{

constexpr ModifierFlags kModNum = 0x1000;
constexpr ModifierFlags kModScroll = 0x8000;

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

TEST_CASE("kanban vim navigation maps selection movement", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_H)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_L)) == KanbanNavigationCommand::SelectRight);
    REQUIRE(navigation.on_key(key_event(SDLK_K)) == KanbanNavigationCommand::SelectUp);
    REQUIRE(navigation.on_key(key_event(SDLK_J)) == KanbanNavigationCommand::SelectDown);
}

TEST_CASE("kanban shifted navigation maps card movement", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_COMMA, kModShift)) == KanbanNavigationCommand::MoveLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_PERIOD, kModShift)) == KanbanNavigationCommand::MoveRight);
    REQUIRE(navigation.on_key(key_event(SDLK_UP, kModShift)) == KanbanNavigationCommand::MoveUp);
    REQUIRE(navigation.on_key(key_event(SDLK_DOWN, kModShift)) == KanbanNavigationCommand::MoveDown);
    REQUIRE(navigation.on_key(key_event(SDLK_H, kModShift)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_L, kModShift)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_K, kModShift)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_J, kModShift)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_LEFT, kModShift)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_RIGHT, kModShift)) == KanbanNavigationCommand::None);
}

TEST_CASE("kanban vim navigation maps page and edge jumps", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_F, kModCtrl)) == KanbanNavigationCommand::SelectPageDown);
    REQUIRE(navigation.on_key(key_event(SDLK_B, kModCtrl)) == KanbanNavigationCommand::SelectPageUp);
    REQUIRE(navigation.on_key(key_event(SDLK_G, kModShift)) == KanbanNavigationCommand::SelectLast);
    REQUIRE(navigation.on_key(key_event(SDLK_G)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_G)) == KanbanNavigationCommand::SelectFirst);
}

TEST_CASE("kanban navigation maps arrows enter and reload", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_LEFT)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_RIGHT)) == KanbanNavigationCommand::SelectRight);
    REQUIRE(navigation.on_key(key_event(SDLK_UP)) == KanbanNavigationCommand::SelectUp);
    REQUIRE(navigation.on_key(key_event(SDLK_DOWN)) == KanbanNavigationCommand::SelectDown);
    REQUIRE(navigation.on_key(key_event(SDLK_RETURN)) == KanbanNavigationCommand::Open);
    REQUIRE(navigation.on_key(key_event(SDLK_KP_ENTER)) == KanbanNavigationCommand::Open);
    REQUIRE(navigation.on_key(key_event(SDLK_R)) == KanbanNavigationCommand::Reload);
}

TEST_CASE("kanban navigation ignores caps lock modifier state", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_H, kModCaps)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_J, kModCaps)) == KanbanNavigationCommand::SelectDown);
    REQUIRE(navigation.on_key(key_event(SDLK_K, kModCaps)) == KanbanNavigationCommand::SelectUp);
    REQUIRE(navigation.on_key(key_event(SDLK_L, kModCaps)) == KanbanNavigationCommand::SelectRight);
    REQUIRE(navigation.on_key(key_event(SDLK_LEFT, kModCaps)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_RETURN, kModCaps)) == KanbanNavigationCommand::Open);
    REQUIRE(navigation.on_key(key_event(SDLK_R, kModCaps)) == KanbanNavigationCommand::Reload);
    REQUIRE(navigation.on_key(key_event(SDLK_COMMA, kModShift | kModCaps)) == KanbanNavigationCommand::MoveLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_LEFT, kModShift | kModCaps)) == KanbanNavigationCommand::None);
}

TEST_CASE("kanban navigation ignores num and scroll lock modifier state", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_H, kModNum)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_J, kModNum)) == KanbanNavigationCommand::SelectDown);
    REQUIRE(navigation.on_key(key_event(SDLK_LEFT, kModNum)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(navigation.on_key(key_event(SDLK_RETURN, kModNum)) == KanbanNavigationCommand::Open);
    REQUIRE(navigation.on_key(key_event(SDLK_R, kModNum)) == KanbanNavigationCommand::Reload);
    REQUIRE(navigation.on_key(key_event(SDLK_L, kModScroll)) == KanbanNavigationCommand::SelectRight);
}

TEST_CASE("kanban navigation ignores releases and unsupported modifiers", "[kanban][navigation]")
{
    KanbanNavigationState navigation;

    REQUIRE(navigation.on_key(key_event(SDLK_H, kModNone, false)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_H, kModCtrl)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_H, kModShift | kModCtrl)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_H, 0x4000)) == KanbanNavigationCommand::None);
    REQUIRE(navigation.on_key(key_event(SDLK_H, kModShift | 0x4000)) == KanbanNavigationCommand::None);
}

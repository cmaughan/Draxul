#include <draxul/kanban/kanban_navigation.h>

#include <draxul/input_types.h>

#include <SDL3/SDL.h>

namespace draxul::kanban
{
namespace
{

ModifierFlags normalize_modifiers(ModifierFlags mod)
{
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

bool has_only_modifiers(ModifierFlags actual, ModifierFlags expected)
{
    constexpr ModifierFlags kModNum = 0x1000;
    constexpr ModifierFlags kModScroll = 0x8000;
    constexpr ModifierFlags kAcceptedModifiers = kGuiModifierMask | kModCaps | kModNum | kModScroll;
    return (actual & ~kAcceptedModifiers) == 0 && normalize_modifiers(actual) == expected;
}

KanbanNavigationCommand movement_command(int keycode, bool move)
{
    switch (keycode)
    {
    case SDLK_H:
    case SDLK_LEFT:
        return move ? KanbanNavigationCommand::MoveLeft : KanbanNavigationCommand::SelectLeft;
    case SDLK_L:
    case SDLK_RIGHT:
        return move ? KanbanNavigationCommand::MoveRight : KanbanNavigationCommand::SelectRight;
    case SDLK_K:
    case SDLK_UP:
        return move ? KanbanNavigationCommand::MoveUp : KanbanNavigationCommand::SelectUp;
    case SDLK_J:
    case SDLK_DOWN:
        return move ? KanbanNavigationCommand::MoveDown : KanbanNavigationCommand::SelectDown;
    default:
        return KanbanNavigationCommand::None;
    }
}

} // namespace

KanbanNavigationCommand KanbanNavigationState::on_key(const draxul::KeyEvent& event)
{
    if (!event.pressed)
        return KanbanNavigationCommand::None;

    if (has_only_modifiers(event.mod, kModShift))
        return movement_command(event.keycode, true);

    if (!has_only_modifiers(event.mod, kModNone))
        return KanbanNavigationCommand::None;

    const auto movement = movement_command(event.keycode, false);
    if (movement != KanbanNavigationCommand::None)
        return movement;

    switch (event.keycode)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return KanbanNavigationCommand::Open;
    case SDLK_R:
        return KanbanNavigationCommand::Reload;
    default:
        return KanbanNavigationCommand::None;
    }
}

} // namespace draxul::kanban

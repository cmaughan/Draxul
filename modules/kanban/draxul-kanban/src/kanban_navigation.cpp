#include <draxul/kanban/kanban_navigation.h>

#include <draxul/input_types.h>

#include <SDL3/SDL.h>

namespace draxul::kanban
{
namespace
{

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

KanbanNavigationCommand selection_movement_command(int keycode)
{
    return movement_command(keycode, false);
}

} // namespace

KanbanNavigationCommand KanbanNavigationState::on_key(const draxul::KeyEvent& event)
{
    if (!event.pressed)
        return KanbanNavigationCommand::None;

    if (has_only_modifiers(event.mod, kModCtrl))
    {
        pending_g_ = false;
        if (event.keycode == SDLK_F)
            return KanbanNavigationCommand::SelectPageDown;
        if (event.keycode == SDLK_B)
            return KanbanNavigationCommand::SelectPageUp;
        return KanbanNavigationCommand::None;
    }

    if (has_only_modifiers(event.mod, kModShift))
    {
        pending_g_ = false;
        switch (event.keycode)
        {
        case SDLK_COMMA:
            return KanbanNavigationCommand::MoveLeft;
        case SDLK_PERIOD:
            return KanbanNavigationCommand::MoveRight;
        case SDLK_UP:
            return KanbanNavigationCommand::MoveUp;
        case SDLK_DOWN:
            return KanbanNavigationCommand::MoveDown;
        case SDLK_G:
            return KanbanNavigationCommand::SelectLast;
        default:
            return KanbanNavigationCommand::None;
        }
    }

    if (!has_only_modifiers(event.mod, kModNone))
    {
        pending_g_ = false;
        return KanbanNavigationCommand::None;
    }

    const auto movement = selection_movement_command(event.keycode);
    if (movement != KanbanNavigationCommand::None)
    {
        pending_g_ = false;
        return movement;
    }

    switch (event.keycode)
    {
    case SDLK_G:
        if (pending_g_)
        {
            pending_g_ = false;
            return KanbanNavigationCommand::SelectFirst;
        }
        pending_g_ = true;
        return KanbanNavigationCommand::None;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        pending_g_ = false;
        return KanbanNavigationCommand::Open;
    case SDLK_R:
        pending_g_ = false;
        return KanbanNavigationCommand::Reload;
    case SDLK_Z:
        pending_g_ = false;
        return KanbanNavigationCommand::ToggleColumnZoom;
    case SDLK_P:
        pending_g_ = false;
        return KanbanNavigationCommand::TogglePreview;
    default:
        pending_g_ = false;
        return KanbanNavigationCommand::None;
    }
}

void KanbanNavigationState::reset()
{
    pending_g_ = false;
}

} // namespace draxul::kanban

#include <draxul/markdown/markdown_navigation.h>

#include <draxul/input_types.h>

#include <SDL3/SDL.h>

namespace draxul::markdown
{

MarkdownNavigationCommand MarkdownNavigationState::on_key(const draxul::KeyEvent& event)
{
    if (!event.pressed)
        return MarkdownNavigationCommand::None;

    if (has_only_modifiers(event.mod, kModCtrl))
    {
        pending_g_ = false;
        if (event.keycode == SDLK_F)
            return MarkdownNavigationCommand::PageDown;
        if (event.keycode == SDLK_B)
            return MarkdownNavigationCommand::PageUp;
        return MarkdownNavigationCommand::None;
    }

    if (has_only_modifiers(event.mod, kModShift) && event.keycode == SDLK_G)
    {
        pending_g_ = false;
        return MarkdownNavigationCommand::Bottom;
    }

    if (!has_only_modifiers(event.mod, kModNone))
    {
        pending_g_ = false;
        return MarkdownNavigationCommand::None;
    }

    switch (event.keycode)
    {
    case SDLK_J:
    case SDLK_DOWN:
        pending_g_ = false;
        return MarkdownNavigationCommand::LineDown;
    case SDLK_K:
    case SDLK_UP:
        pending_g_ = false;
        return MarkdownNavigationCommand::LineUp;
    case SDLK_PAGEDOWN:
    case SDLK_SPACE:
        pending_g_ = false;
        return MarkdownNavigationCommand::PageDown;
    case SDLK_PAGEUP:
        pending_g_ = false;
        return MarkdownNavigationCommand::PageUp;
    case SDLK_HOME:
        pending_g_ = false;
        return MarkdownNavigationCommand::Top;
    case SDLK_END:
        pending_g_ = false;
        return MarkdownNavigationCommand::Bottom;
    case SDLK_G:
        if (pending_g_)
        {
            pending_g_ = false;
            return MarkdownNavigationCommand::Top;
        }
        pending_g_ = true;
        return MarkdownNavigationCommand::None;
    default:
        pending_g_ = false;
        return MarkdownNavigationCommand::None;
    }
}

void MarkdownNavigationState::reset()
{
    pending_g_ = false;
}

} // namespace draxul::markdown

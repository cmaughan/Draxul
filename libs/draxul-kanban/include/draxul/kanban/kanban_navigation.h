#pragma once

#include <draxul/events.h>

namespace draxul::kanban
{

enum class KanbanNavigationCommand
{
    None,
    SelectLeft,
    SelectRight,
    SelectUp,
    SelectDown,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Open,
    Reload,
};

class KanbanNavigationState
{
public:
    KanbanNavigationCommand on_key(const draxul::KeyEvent& event);
};

} // namespace draxul::kanban

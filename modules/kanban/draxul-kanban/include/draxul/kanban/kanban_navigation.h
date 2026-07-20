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
    SelectPageUp,
    SelectPageDown,
    SelectFirst,
    SelectLast,
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
    void reset();

private:
    bool pending_g_ = false;
};

} // namespace draxul::kanban

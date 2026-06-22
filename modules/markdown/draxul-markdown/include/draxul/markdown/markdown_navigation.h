#pragma once

#include <draxul/events.h>

namespace draxul::markdown
{

enum class MarkdownNavigationCommand
{
    None,
    LineDown,
    LineUp,
    PageDown,
    PageUp,
    Top,
    Bottom,
};

class MarkdownNavigationState
{
public:
    MarkdownNavigationCommand on_key(const draxul::KeyEvent& event);
    void reset();

private:
    bool pending_g_ = false;
};

} // namespace draxul::markdown

#pragma once

#include <draxul/types.h>

namespace draxul
{

struct VtState
{
    int col = 0;
    int row = 0;
    int saved_col = 0;
    int saved_row = 0;
    bool cursor_visible = true;
    bool pending_wrap = false;
    int scroll_top = 0;
    int scroll_bottom = 0;
    bool auto_wrap_mode = true;
    bool origin_mode = false;
    bool cursor_app_mode = false;

    // DECSCUSR — cursor shape set by the shell via CSI Ps SP q.
    CursorShape cursor_shape = CursorShape::Block;
    bool cursor_blink = false; // steady by default; shell sends DECSCUSR to enable
};

} // namespace draxul

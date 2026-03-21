#include <draxul/terminal_host_base.h>

#include <draxul/log.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <charconv>
#include <string_view>
#include <vector>

namespace draxul
{

void TerminalHostBase::handle_control(char ch)
{
    if (ch == '\r')
    {
        vt_.col = 0;
        vt_.pending_wrap = false;
    }
    else if (ch == '\n')
    {
        newline(false);
    }
    else if (ch == '\b')
    {
        vt_.col = std::max(0, vt_.col - 1);
        vt_.pending_wrap = false;
    }
    else if (ch == '\t')
    {
        vt_.col = std::min(std::max(0, grid_cols() - 1), ((vt_.col / 8) + 1) * 8);
        vt_.pending_wrap = false;
    }
    set_cursor_position(vt_.col, vt_.row);
}

void TerminalHostBase::handle_esc(char ch)
{
    if (ch == '7') // DECSC - Save Cursor
    {
        vt_.saved_col = vt_.col;
        vt_.saved_row = vt_.row;
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "DECSC save row=%d col=%d", vt_.row, vt_.col);
    }
    else if (ch == '8') // DECRC - Restore Cursor
    {
        vt_.col = vt_.saved_col;
        vt_.row = vt_.saved_row;
        vt_.pending_wrap = false;
        set_cursor_position(vt_.col, vt_.row);
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "DECRC restore row=%d col=%d", vt_.row, vt_.col);
    }
    else
    {
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "ESC unhandled: '%c' (0x%02X)", ch, (unsigned char)ch);
    }
}

void TerminalHostBase::handle_csi(char final_char, std::string_view body)
{
    bool private_mode = !body.empty() && body.front() == '?';
    if (private_mode)
        body.remove_prefix(1);

    std::vector<int> params;
    size_t start = 0;
    while (start <= body.size())
    {
        const size_t semi = body.find(';', start);
        const std::string_view part
            = semi == std::string_view::npos ? body.substr(start) : body.substr(start, semi - start);
        // std::from_chars is intentionally equivalent to atoi: returns 0 on parse error,
        // no heap allocation, no locale dependency. Requires C++17.
        int value = 0;
        if (!part.empty())
            std::from_chars(part.data(), part.data() + part.size(), value);
        params.push_back(value);
        if (semi == std::string_view::npos)
            break;
        start = semi + 1;
    }

    switch (final_char)
    {
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'Z':
    case 'd':
    case 'f':
    case 's':
    case 'u':
        csi_cursor_move(final_char, params);
        break;
    case 'J':
    case 'K':
    case 'X':
        csi_erase(final_char, params);
        break;
    case 'S':
    case 'T':
        csi_scroll(final_char, private_mode, params);
        break;
    case 'L':
    case 'M':
    case '@':
    case 'P':
        csi_insert_delete(final_char, params);
        break;
    case 'm':
        csi_sgr(params);
        break;
    case 'h':
    case 'l':
        csi_mode(final_char, private_mode, params);
        break;
    case 'r':
        csi_margins(private_mode, params);
        break;
    default:
        break;
    }

    set_cursor_position(vt_.col, vt_.row);
}

void TerminalHostBase::csi_cursor_move(char final_char, const std::vector<int>& params)
{
    auto param_or = [&](size_t index, int fallback) {
        return index < params.size() && params[index] > 0 ? params[index] : fallback;
    };

    switch (final_char)
    {
    case 'A': // CUU - Cursor Up
        vt_.row = std::max(vt_.origin_mode ? vt_.scroll_top : 0, vt_.row - param_or(0, 1));
        vt_.pending_wrap = false;
        break;
    case 'B': // CUD - Cursor Down
        vt_.row = std::min(vt_.origin_mode ? vt_.scroll_bottom : std::max(0, grid_rows() - 1),
            vt_.row + param_or(0, 1));
        vt_.pending_wrap = false;
        break;
    case 'C': // CUF - Cursor Forward
        vt_.col = std::min(std::max(0, grid_cols() - 1), vt_.col + param_or(0, 1));
        vt_.pending_wrap = false;
        break;
    case 'D': // CUB - Cursor Back
        vt_.col = std::max(0, vt_.col - param_or(0, 1));
        vt_.pending_wrap = false;
        break;
    case 'E': // CNL - Cursor Next Line
        vt_.row = std::min(vt_.scroll_bottom, vt_.row + param_or(0, 1));
        vt_.col = 0;
        vt_.pending_wrap = false;
        break;
    case 'F': // CPL - Cursor Preceding Line
        vt_.row = std::max(vt_.scroll_top, vt_.row - param_or(0, 1));
        vt_.col = 0;
        vt_.pending_wrap = false;
        break;
    case 'G': // CHA - Cursor Horizontal Absolute
        vt_.col = std::clamp(param_or(0, 1) - 1, 0, std::max(0, grid_cols() - 1));
        vt_.pending_wrap = false;
        break;
    case 'H': // CUP - Cursor Position
    case 'f': // HVP
    {
        const int r = param_or(0, 1) - 1;
        const int c = param_or(1, 1) - 1;
        if (vt_.origin_mode)
        {
            vt_.row = std::clamp(r + vt_.scroll_top, vt_.scroll_top, vt_.scroll_bottom);
            vt_.col = std::clamp(c, 0, std::max(0, grid_cols() - 1));
        }
        else
        {
            vt_.row = std::clamp(r, 0, std::max(0, grid_rows() - 1));
            vt_.col = std::clamp(c, 0, std::max(0, grid_cols() - 1));
        }
        DRAXUL_LOG_TRACE(draxul::LogCategory::App,
            "CUP param(%d,%d) origin=%d stbm=[%d..%d] grid_rows=%d -> row=%d col=%d",
            r, c, (int)vt_.origin_mode, vt_.scroll_top, vt_.scroll_bottom,
            grid_rows(), vt_.row, vt_.col);
        vt_.pending_wrap = false;
        break;
    }
    case 'I': // CHT - Cursor Forward Tabulation
    {
        const int n = param_or(0, 1);
        for (int i = 0; i < n; ++i)
            vt_.col = std::min(std::max(0, grid_cols() - 1), ((vt_.col / 8) + 1) * 8);
        vt_.pending_wrap = false;
        break;
    }
    case 'Z': // CBT - Cursor Backward Tabulation
    {
        const int n = param_or(0, 1);
        for (int i = 0; i < n; ++i)
        {
            const int prev = ((vt_.col - 1) / 8) * 8;
            vt_.col = std::max(0, prev);
        }
        vt_.pending_wrap = false;
        break;
    }
    case 'd': // VPA - Vertical Position Absolute
        vt_.row = std::clamp(param_or(0, 1) - 1, 0, std::max(0, grid_rows() - 1));
        vt_.pending_wrap = false;
        break;
    case 's': // SCOSC - Save Cursor
        vt_.saved_col = vt_.col;
        vt_.saved_row = vt_.row;
        break;
    case 'u': // SCORC - Restore Cursor
        vt_.col = vt_.saved_col;
        vt_.row = vt_.saved_row;
        vt_.pending_wrap = false;
        break;
    default:
        break;
    }
}

void TerminalHostBase::csi_erase(char final_char, const std::vector<int>& params)
{
    auto param_or = [&](size_t index, int fallback) {
        return index < params.size() && params[index] > 0 ? params[index] : fallback;
    };

    switch (final_char)
    {
    case 'J': // ED - Erase in Display
        erase_display(param_or(0, 0));
        break;
    case 'K': // EL - Erase in Line
        erase_line(param_or(0, 0));
        break;
    case 'X': // ECH - Erase Character
    {
        const int n = param_or(0, 1);
        const int end = std::min(grid_cols(), vt_.col + n);
        for (int col = vt_.col; col < end; ++col)
            clear_cell(col, vt_.row);
        break;
    }
    default:
        break;
    }
}

void TerminalHostBase::csi_scroll(char final_char, bool private_mode, const std::vector<int>& params)
{
    auto param_or = [&](size_t index, int fallback) {
        return index < params.size() && params[index] > 0 ? params[index] : fallback;
    };

    switch (final_char)
    {
    case 'S': // SU - Scroll Up
    {
        const int n = param_or(0, 1);
        if (!private_mode)
            grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), n);
        break;
    }
    case 'T': // SD - Scroll Down
    {
        const int n = param_or(0, 1);
        if (!private_mode)
            grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), -n);
        break;
    }
    default:
        break;
    }
}

void TerminalHostBase::csi_insert_delete(char final_char, const std::vector<int>& params)
{
    auto param_or = [&](size_t index, int fallback) {
        return index < params.size() && params[index] > 0 ? params[index] : fallback;
    };

    switch (final_char)
    {
    case 'L': // IL - Insert Line
    {
        const int n = param_or(0, 1);
        if (vt_.row >= vt_.scroll_top && vt_.row <= vt_.scroll_bottom)
            grid().scroll(vt_.row, vt_.scroll_bottom + 1, 0, grid_cols(), -n);
        vt_.pending_wrap = false;
        break;
    }
    case 'M': // DL - Delete Line
    {
        const int n = param_or(0, 1);
        if (vt_.row >= vt_.scroll_top && vt_.row <= vt_.scroll_bottom)
            grid().scroll(vt_.row, vt_.scroll_bottom + 1, 0, grid_cols(), n);
        vt_.pending_wrap = false;
        break;
    }
    case '@': // ICH - Insert Character
    {
        const int n = param_or(0, 1);
        grid().scroll(vt_.row, vt_.row + 1, vt_.col, grid_cols(), 0, -n);
        break;
    }
    case 'P': // DCH - Delete Character
    {
        const int n = param_or(0, 1);
        grid().scroll(vt_.row, vt_.row + 1, vt_.col, grid_cols(), 0, n);
        break;
    }
    default:
        break;
    }
}

void TerminalHostBase::csi_sgr(const std::vector<int>& params)
{
    apply_sgr(current_attr_, params);
}

void TerminalHostBase::csi_mode(char final_char, bool private_mode, const std::vector<int>& params)
{
    if (!private_mode)
        return;

    const bool enable = final_char == 'h';
    for (const int mode : params)
    {
        switch (mode)
        {
        case 1: // DECCKM - Application Cursor Keys
            vt_.cursor_app_mode = enable;
            break;
        case 6: // DECOM - Origin Mode
            vt_.origin_mode = enable;
            vt_.row = enable ? vt_.scroll_top : 0;
            vt_.col = 0;
            vt_.pending_wrap = false;
            DRAXUL_LOG_TRACE(draxul::LogCategory::App,
                "DECOM %s stbm=[%d..%d] -> row=%d",
                enable ? "ON" : "OFF", vt_.scroll_top, vt_.scroll_bottom, vt_.row);
            break;
        case 7: // DECAWM - Auto Wrap Mode
            vt_.auto_wrap_mode = enable;
            break;
        case 25: // DECTCEM - Cursor Visible
            vt_.cursor_visible = enable;
            update_cursor_style();
            break;
        case 47:
        case 1047:
        case 1049:
            if (enable)
                enter_alt_screen();
            else
                leave_alt_screen();
            break;
        case 1000: // X10/Normal tracking
        case 1002: // Button motion
        case 1003: // Any motion
        case 1006: // SGR extended format
            on_mouse_mode_changed(mode, enable);
            break;
        case 2004: // Bracketed paste mode
            bracketed_paste_mode_ = enable;
            break;
        default:
            break;
        }
    }
}

void TerminalHostBase::csi_margins(bool private_mode, const std::vector<int>& params)
{
    auto param_or = [&](size_t index, int fallback) {
        return index < params.size() && params[index] > 0 ? params[index] : fallback;
    };

    // DECSTBM - Set Top and Bottom Margins (scroll region)
    if (!private_mode)
    {
        const int top = std::clamp(param_or(0, 1) - 1, 0, grid_rows() - 1);
        const int bot = std::clamp(param_or(1, grid_rows()) - 1, 0, grid_rows() - 1);
        if (top < bot)
        {
            vt_.scroll_top = top;
            vt_.scroll_bottom = bot;
        }
        vt_.row = vt_.origin_mode ? vt_.scroll_top : 0;
        vt_.col = 0;
        vt_.pending_wrap = false;
        DRAXUL_LOG_TRACE(draxul::LogCategory::App,
            "DECSTBM params(%d,%d) grid_rows=%d -> stbm=[%d..%d] row=%d origin=%d",
            param_or(0, 1), param_or(1, grid_rows()), grid_rows(),
            vt_.scroll_top, vt_.scroll_bottom, vt_.row, (int)vt_.origin_mode);
    }
}

void TerminalHostBase::handle_osc(std::string_view body) const
{
    const size_t semi = body.find(';');
    if (semi == std::string_view::npos)
        return;

    const std::string_view code = body.substr(0, semi);
    if ((code == "0" || code == "2") && callbacks().set_window_title)
        callbacks().set_window_title(std::string(body.substr(semi + 1)));
}

void TerminalHostBase::consume_output(std::string_view bytes)
{
    mark_activity();
    vt_parser_.feed(bytes);
}

} // namespace draxul

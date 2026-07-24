#include <draxul/terminal_host_base.h>

#include <draxul/base64.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/terminal_sgr.h>
#include <draxul/window.h>

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// Return params[index] when present and positive, otherwise return fallback.
// Shared helper used by all CSI dispatch functions that interpret numeric parameters.
static int param_or(const std::vector<int>& params, size_t index, int fallback)
{
    return index < params.size() && params[index] > 0 ? params[index] : fallback;
}

} // anonymous namespace

namespace draxul
{

void TerminalHostBase::handle_control(char ch)
{
    PERF_MEASURE();
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
    else if (ch == '\x0E') // SO - Shift Out (select G1 into GL)
    {
        gl_uses_g1_charset_ = true;
        vt_.pending_wrap = false;
    }
    else if (ch == '\x0F') // SI - Shift In (select G0 into GL)
    {
        gl_uses_g1_charset_ = false;
        vt_.pending_wrap = false;
    }
}

void TerminalHostBase::handle_esc(char ch)
{
    PERF_MEASURE();
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
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "DECRC restore row=%d col=%d", vt_.row, vt_.col);
    }
    else if (ch == 'D') // IND - Index: move cursor down, scroll up at scroll_bottom
    {
        vt_.pending_wrap = false;
        if (vt_.row == vt_.scroll_bottom)
        {
            if (!alt_screen_.in_alt_screen() && vt_.scroll_top == 0
                && vt_.scroll_bottom == grid_rows() - 1)
            {
                on_line_scrolled_off(vt_.scroll_top);
            }
            grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), 1);
        }
        else if (vt_.row < grid_rows() - 1)
        {
            ++vt_.row;
        }
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "IND row=%d stbm=[%d..%d]", vt_.row, vt_.scroll_top, vt_.scroll_bottom);
    }
    else if (ch == 'M') // RI - Reverse Index: move cursor up, scroll down at scroll_top
    {
        vt_.pending_wrap = false;
        if (vt_.row == vt_.scroll_top)
        {
            grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), -1);
        }
        else if (vt_.row > 0)
        {
            --vt_.row;
        }
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "RI row=%d stbm=[%d..%d]", vt_.row, vt_.scroll_top, vt_.scroll_bottom);
    }
    else if (ch == '(' || ch == ')') // SCS - designate G0/G1 charset
    {
        pending_charset_designation_ = ch;
    }
    else
    {
        DRAXUL_LOG_TRACE(draxul::LogCategory::App, "ESC unhandled: '%c' (0x%02X)", ch, (unsigned char)ch);
    }
}

void TerminalHostBase::handle_csi(char final_char, std::string_view body)
{
    PERF_MEASURE();
    DRAXUL_LOG_DEBUG(LogCategory::App, "terminal: CSI %.*s %c",
        static_cast<int>(body.size()), body.data(), final_char);
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
    case 'n':
        csi_dsr(private_mode, params);
        break;
    case 'c':
        csi_da(private_mode, params);
        break;
    case 'r':
        csi_margins(private_mode, params);
        break;
    case 'q':
        // DECSCUSR (CSI Ps SP q) — Set Cursor Style.
        // The space intermediate byte is included in the body, so check for it.
        if (!body.empty() && body.back() == ' ')
        {
            const int ps = params.empty() ? 0 : params[0];
            switch (ps)
            {
            case 0: // default → blinking block
            case 1:
                vt_.cursor_shape = CursorShape::Block;
                vt_.cursor_blink = true;
                break;
            case 2:
                vt_.cursor_shape = CursorShape::Block;
                vt_.cursor_blink = false;
                break;
            case 3:
                vt_.cursor_shape = CursorShape::Horizontal;
                vt_.cursor_blink = true;
                break;
            case 4:
                vt_.cursor_shape = CursorShape::Horizontal;
                vt_.cursor_blink = false;
                break;
            case 5:
                vt_.cursor_shape = CursorShape::Vertical;
                vt_.cursor_blink = true;
                break;
            case 6:
                vt_.cursor_shape = CursorShape::Vertical;
                vt_.cursor_blink = false;
                break;
            default:
                break;
            }
            update_cursor_style();
        }
        break;
    default:
        break;
    }
}

void TerminalHostBase::csi_cursor_move(char final_char, const std::vector<int>& params)
{
    PERF_MEASURE();

    switch (final_char)
    {
    case 'A': // CUU - Cursor Up
        vt_.row = std::max(vt_.origin_mode ? vt_.scroll_top : 0, vt_.row - param_or(params, 0, 1));
        vt_.pending_wrap = false;
        break;
    case 'B': // CUD - Cursor Down
        vt_.row = std::min(vt_.origin_mode ? vt_.scroll_bottom : std::max(0, grid_rows() - 1),
            vt_.row + param_or(params, 0, 1));
        vt_.pending_wrap = false;
        break;
    case 'C': // CUF - Cursor Forward
        vt_.col = std::min(std::max(0, grid_cols() - 1), vt_.col + param_or(params, 0, 1));
        vt_.pending_wrap = false;
        break;
    case 'D': // CUB - Cursor Back
        vt_.col = std::max(0, vt_.col - param_or(params, 0, 1));
        vt_.pending_wrap = false;
        break;
    case 'E': // CNL - Cursor Next Line
        vt_.row = std::min(vt_.scroll_bottom, vt_.row + param_or(params, 0, 1));
        vt_.col = 0;
        vt_.pending_wrap = false;
        break;
    case 'F': // CPL - Cursor Preceding Line
        vt_.row = std::max(vt_.scroll_top, vt_.row - param_or(params, 0, 1));
        vt_.col = 0;
        vt_.pending_wrap = false;
        break;
    case 'G': // CHA - Cursor Horizontal Absolute
        vt_.col = std::clamp(param_or(params, 0, 1) - 1, 0, std::max(0, grid_cols() - 1));
        vt_.pending_wrap = false;
        break;
    case 'H': // CUP - Cursor Position
    case 'f': // HVP
    {
        const int r = param_or(params, 0, 1) - 1;
        const int c = param_or(params, 1, 1) - 1;
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
        const int n = param_or(params, 0, 1);
        for (int i = 0; i < n; ++i)
            vt_.col = std::min(std::max(0, grid_cols() - 1), ((vt_.col / 8) + 1) * 8);
        vt_.pending_wrap = false;
        break;
    }
    case 'Z': // CBT - Cursor Backward Tabulation
    {
        const int n = param_or(params, 0, 1);
        for (int i = 0; i < n; ++i)
        {
            const int prev = ((vt_.col - 1) / 8) * 8;
            vt_.col = std::max(0, prev);
        }
        vt_.pending_wrap = false;
        break;
    }
    case 'd': // VPA - Vertical Position Absolute
        vt_.row = std::clamp(param_or(params, 0, 1) - 1, 0, std::max(0, grid_rows() - 1));
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
    PERF_MEASURE();

    switch (final_char)
    {
    case 'J': // ED - Erase in Display
        erase_display(param_or(params, 0, 0));
        break;
    case 'K': // EL - Erase in Line
        erase_line(param_or(params, 0, 0));
        break;
    case 'X': // ECH - Erase Character
    {
        const int n = param_or(params, 0, 1);
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
    PERF_MEASURE();

    switch (final_char)
    {
    case 'S': // SU - Scroll Up
    {
        const int n = param_or(params, 0, 1);
        if (!private_mode)
        {
            // Capture rows about to scroll off the top into scrollback.
            if (!alt_screen_.in_alt_screen() && vt_.scroll_top == 0
                && vt_.scroll_bottom == grid_rows() - 1)
            {
                const int capture = std::min(n, grid_rows());
                for (int i = 0; i < capture; ++i)
                    on_line_scrolled_off(vt_.scroll_top + i);
            }
            grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), n);
        }
        break;
    }
    case 'T': // SD - Scroll Down
    {
        const int n = param_or(params, 0, 1);
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
    PERF_MEASURE();

    switch (final_char)
    {
    case 'L': // IL - Insert Line
    {
        const int n = param_or(params, 0, 1);
        if (vt_.row >= vt_.scroll_top && vt_.row <= vt_.scroll_bottom)
            grid().scroll(vt_.row, vt_.scroll_bottom + 1, 0, grid_cols(), -n);
        vt_.pending_wrap = false;
        break;
    }
    case 'M': // DL - Delete Line
    {
        const int n = param_or(params, 0, 1);
        if (vt_.row >= vt_.scroll_top && vt_.row <= vt_.scroll_bottom)
        {
            // Capture rows scrolling off the top when deleting at the
            // top of the full-screen scroll region.
            if (!alt_screen_.in_alt_screen() && vt_.row == 0 && vt_.scroll_top == 0
                && vt_.scroll_bottom == grid_rows() - 1)
            {
                const int capture = std::min(n, grid_rows());
                for (int i = 0; i < capture; ++i)
                    on_line_scrolled_off(i);
            }
            grid().scroll(vt_.row, vt_.scroll_bottom + 1, 0, grid_cols(), n);
        }
        vt_.pending_wrap = false;
        break;
    }
    case '@': // ICH - Insert Character
    {
        const int n = param_or(params, 0, 1);
        grid().scroll(vt_.row, vt_.row + 1, vt_.col, grid_cols(), 0, -n);
        break;
    }
    case 'P': // DCH - Delete Character
    {
        const int n = param_or(params, 0, 1);
        grid().scroll(vt_.row, vt_.row + 1, vt_.col, grid_cols(), 0, n);
        break;
    }
    default:
        break;
    }
}

void TerminalHostBase::csi_sgr(const std::vector<int>& params)
{
    PERF_MEASURE();
    apply_sgr(current_attr_, params);
}

void TerminalHostBase::csi_mode(char final_char, bool private_mode, const std::vector<int>& params)
{
    PERF_MEASURE();
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
            if (output_cursor_batch_active_)
            {
                if (enable)
                    output_cursor_batch_saw_show_ = true;
                else
                    output_cursor_batch_saw_hide_ = true;
            }
            if (synchronized_output_active())
            {
                if (enable)
                    synchronized_output_saw_show_ = true;
                else
                    synchronized_output_saw_hide_ = true;
            }
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            {
                log_printf(LogLevel::Trace,
                    LogCategory::Input,
                    "cursor trace: dectcem enable=%d alt_screen=%d logical=(%d,%d)",
                    enable ? 1 : 0,
                    alt_screen_.in_alt_screen() ? 1 : 0,
                    vt_.col,
                    vt_.row);
            }
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
        case 1004: // Focus reporting
        case 1006: // SGR extended format
            if (mode == 1004)
                focus_reporting_mode_ = enable;
            else
                on_mouse_mode_changed(mode, enable);
            break;
        case 2004: // Bracketed paste mode
            bracketed_paste_mode_ = enable;
            break;
        case 2026: // Synchronized output mode
            if (enable)
                begin_synchronized_output();
            else
                end_synchronized_output();
            break;
        default:
            break;
        }
    }
}

void TerminalHostBase::csi_dsr(bool private_mode, const std::vector<int>& params)
{
    PERF_MEASURE();
    if (private_mode)
        return;
    const int code = params.empty() ? 0 : params[0];
    if (code == 6)
    {
        // CPR — Cursor Position Report: reply ESC [ row ; col R (1-based)
        std::string response(32, '\0');
        const int n = std::snprintf(response.data(), response.size(), "\x1B[%d;%dR", vt_.row + 1, vt_.col + 1);
        if (n > 0)
            do_process_write(std::string_view(response.data(), static_cast<size_t>(n)));
    }
    else if (code == 5)
    {
        // DSR — Device Status Report: reply "OK"
        do_process_write("\x1B[0n");
    }
}

void TerminalHostBase::csi_da(bool private_mode, const std::vector<int>& params)
{
    PERF_MEASURE();
    const int code = params.empty() ? 0 : params[0];
    if (!private_mode && code == 0)
    {
        // DA1 — Primary Device Attributes: claim VT220 with ANSI color
        do_process_write("\x1B[?62;22c");
    }
    else if (private_mode && code == 0)
    {
        // DA2 — Secondary Device Attributes: "VT220, firmware 1.0"
        do_process_write("\x1B[>1;10;0c");
    }
}

void TerminalHostBase::csi_margins(bool private_mode, const std::vector<int>& params)
{
    PERF_MEASURE();

    // DECSTBM - Set Top and Bottom Margins (scroll region)
    if (!private_mode)
    {
        // Guard against a zero-sized grid (e.g. window minimized mid-redraw).
        // std::clamp(x, 0, -1) is UB — require at least one row/col before clamping.
        if (grid_rows() <= 0 || grid_cols() <= 0)
        {
            return;
        }
        const int top = std::clamp(param_or(params, 0, 1) - 1, 0, grid_rows() - 1);
        const int bot = std::clamp(param_or(params, 1, grid_rows()) - 1, 0, grid_rows() - 1);
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
            param_or(params, 0, 1), param_or(params, 1, grid_rows()), grid_rows(),
            vt_.scroll_top, vt_.scroll_bottom, vt_.row, (int)vt_.origin_mode);
    }
}

// Decode percent-encoded bytes in a URI path (e.g. %20 → ' ').
// Only touches %XX sequences; all other characters pass through unchanged.
static std::string percent_decode(std::string_view encoded)
{
    PERF_MEASURE();
    std::string out;
    out.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            const auto hi = encoded[i + 1];
            const auto lo = encoded[i + 2];
            auto hex_digit = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            const int h = hex_digit(hi);
            const int l = hex_digit(lo);
            if (h >= 0 && l >= 0)
            {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(encoded[i]);
    }
    return out;
}

// Extract the filesystem path from an OSC 7 URI (file://hostname/path).
// Returns the decoded absolute path, or empty string if the URI is malformed.
static std::string extract_osc7_path(std::string_view uri)
{
    PERF_MEASURE();
    // Strip the "file://" prefix.
    constexpr std::string_view kFilePrefix = "file://";
    if (!uri.starts_with(kFilePrefix))
        return {};
    uri.remove_prefix(kFilePrefix.size());

    // Skip the hostname — everything up to the next '/'.
    const size_t slash = uri.find('/');
    if (slash == std::string_view::npos)
        return {};
    uri.remove_prefix(slash); // keep the leading '/'

    return percent_decode(uri);
}

void TerminalHostBase::handle_osc(std::string_view body)
{
    PERF_MEASURE();
    const size_t semi = body.find(';');
    if (semi == std::string_view::npos)
        return;

    const std::string_view code = body.substr(0, semi);
    const std::string_view payload = body.substr(semi + 1);

    if (code == "0" || code == "2")
    {
        terminal_title_ = std::string(payload);
        callbacks().set_window_title(std::string(payload));
    }
    else if (code == "8")
    {
        handle_osc8(payload);
    }
    else if (code == "52")
    {
        // OSC 52: clipboard manipulation. Format:
        //   ESC ] 52 ; <selection> ; <base64_data> ST   (write)
        //   ESC ] 52 ; <selection> ; ?                  (read query)
        // <selection> is one or more of c/p/q/s (clipboard, primary, secondary,
        // selection); we treat all of them as the system clipboard.
        const size_t inner = payload.find(';');
        if (inner == std::string_view::npos)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "OSC 52: missing data field, ignoring");
            return;
        }
        const std::string_view data = payload.substr(inner + 1);
        if (data == "?")
        {
            // Read query — base64-encode the system clipboard and write it
            // back to the host with the same selection prefix.
            const std::string clip = window().clipboard_text();
            const std::string encoded = base64_encode(clip);
            std::string response;
            response.reserve(8 + (inner + 1) + encoded.size() + 2);
            response += "\x1B]52;";
            response += payload.substr(0, inner + 1);
            response += encoded;
            response += "\x1B\\";
            do_process_write(response);
            DRAXUL_LOG_DEBUG(LogCategory::App, "OSC 52: replied with %zu bytes of clipboard data",
                clip.size());
            return;
        }
        auto decoded = base64_decode(data);
        if (!decoded.has_value())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "OSC 52: malformed base64 payload (%zu bytes), ignoring", data.size());
            return;
        }
        if (decoded->empty())
        {
            // Some applications send an empty payload to clear the clipboard.
            window().set_clipboard_text("");
            return;
        }
        window().set_clipboard_text(*decoded);
        DRAXUL_LOG_DEBUG(LogCategory::App, "OSC 52: clipboard set (%zu bytes)", decoded->size());
    }
    else if (code == "7")
    {
        if (payload.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App, "OSC 7: empty URI, ignoring");
            return;
        }
        std::string path = extract_osc7_path(payload);
        if (!path.empty())
        {
            DRAXUL_LOG_DEBUG(LogCategory::App, "OSC 7: cwd = %s", path.c_str());
            on_osc_cwd(path);
        }
        else
        {
            DRAXUL_LOG_WARN(LogCategory::App, "OSC 7: malformed URI '%.*s', ignoring",
                static_cast<int>(payload.size()), payload.data());
        }
    }
    else if (code == "133")
    {
        handle_osc133(payload);
    }
}

void TerminalHostBase::handle_osc8(std::string_view payload)
{
    PERF_MEASURE();
    if (!launch_options().enable_osc8_hyperlinks)
    {
        current_hyperlink_id_ = 0;
        return;
    }

    const size_t inner = payload.find(';');
    const std::string_view uri = inner == std::string_view::npos ? payload : payload.substr(inner + 1);
    if (uri.empty())
    {
        current_hyperlink_id_ = 0;
        return;
    }

    current_hyperlink_id_ = grid().link_id_for_uri(uri);
}

void TerminalHostBase::handle_osc133(std::string_view payload)
{
    PERF_MEASURE();
    if (!launch_options().enable_shell_integration_marks || payload.empty())
        return;

    ShellMark mark;
    mark.row = vt_.row;

    switch (payload.front())
    {
    case 'A':
        mark.type = ShellMarkType::PromptStart;
        break;
    case 'B':
        mark.type = ShellMarkType::CommandStart;
        break;
    case 'C':
        mark.type = ShellMarkType::OutputStart;
        break;
    case 'D':
        mark.type = ShellMarkType::OutputEnd;
        if (payload.size() > 2 && payload[1] == ';')
        {
            int exit_code = -1;
            const std::string_view code = payload.substr(2);
            const auto [ptr, ec] = std::from_chars(code.data(), code.data() + code.size(), exit_code);
            if (ec == std::errc{} && ptr == code.data() + code.size())
                mark.exit_code = exit_code;
        }
        break;
    default:
        return;
    }

    shell_marks_.push_back(mark);
}

void TerminalHostBase::consume_output(std::string_view bytes)
{
    PERF_MEASURE();
    if (bytes.empty())
        return;
    resize_preserved_cursor_.reset();
    const bool scoped_batch = !output_cursor_batch_active_;
    if (scoped_batch)
        begin_output_cursor_batch();
    mark_activity();
    vt_parser_.feed(bytes);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: consume_output len=%zu logical=(%d,%d) vt_visible=%d sync_output=%d",
            bytes.size(),
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0,
            synchronized_output_active() ? 1 : 0);
    }
    if (scoped_batch)
        end_output_cursor_batch();
}

} // namespace draxul

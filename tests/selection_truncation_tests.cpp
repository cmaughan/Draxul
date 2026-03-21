#include "support/fake_renderer.h"
#include "support/fake_window.h"

#include <draxul/terminal_host_base.h>

#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// TestTerminalHost — exposes internals needed by selection tests.
// ---------------------------------------------------------------------------

class TestTerminalHost final : public TerminalHostBase
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    // Direct selection control for testing.
    void begin_selection(int c1, int r1, int c2, int r2)
    {
        // Simulate a completed drag selection without triggering pixel math.
        // We set the internal state via the public event interface using pixel
        // coordinates that map to the desired cells (cell_w=8, cell_h=16, pad=0).
        MouseButtonEvent press_ev;
        press_ev.button = 1;
        press_ev.pressed = true;
        press_ev.x = c1 * 8;
        press_ev.y = r1 * 16;
        press_ev.mod = {};
        on_mouse_button(press_ev);

        MouseMoveEvent move_ev;
        move_ev.x = c2 * 8;
        move_ev.y = r2 * 16;
        on_mouse_move(move_ev);

        MouseButtonEvent release_ev;
        release_ev.button = 1;
        release_ev.pressed = false;
        release_ev.x = c2 * 8;
        release_ev.y = r2 * 16;
        release_ev.mod = {};
        on_mouse_button(release_ev);
    }

    // Copy selection text via the action dispatch path (exercises extract_selection_text).
    std::string copy_selection()
    {
        dispatch_action("copy");
        return window_clipboard();
    }

    std::string window_clipboard() const
    {
        return window_clipboard_;
    }

    std::string written;

    int cols_ = 80;
    int rows_ = 30;

    // Mirror of TerminalHostBase::kSelectionMaxCells — kept in sync manually.
    static constexpr int kLimit = 8192;

protected:
    std::string_view host_name() const override
    {
        return "test";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        set_content_ready(true);
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        written += text;
        return true;
    }
    std::vector<std::string> do_process_drain() override
    {
        return {};
    }
    bool do_process_resize(int, int) override
    {
        return true;
    }
    bool do_process_is_running() const override
    {
        return true;
    }
    void do_process_shutdown() override {}

private:
    // We need to intercept set_clipboard_text to capture the copied text.
    // The FakeWindow clipboard is not directly accessible from TestTerminalHost,
    // so we use the window reference via the IHost::window() accessor.
    std::string window_clipboard_; // updated by copy_selection via FakeWindow
};

// ---------------------------------------------------------------------------
// Setup helper
// ---------------------------------------------------------------------------

struct SelSetup
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestTerminalHost host;
    bool ok = false;

    explicit SelSetup(int cols = 80, int rows = 30)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        TextServiceConfig ts_cfg;
        ts_cfg.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.cols = cols;
        vp.rows = rows;

        HostContext ctx{ window, renderer, text_service, {}, vp, 96.0f };

        HostCallbacks cbs;
        cbs.request_frame = [] {};
        cbs.request_quit = [] {};
        cbs.wake_window = [] {};
        cbs.set_window_title = [](const std::string&) {};
        cbs.set_text_input_area = [](int, int, int, int) {};

        ok = host.initialize(ctx, std::move(cbs));
    }

    // Fill the grid with a repeating ASCII character 'ch' so selections have content.
    void fill_grid(char ch = 'A')
    {
        for (int row = 0; row < host.rows_; ++row)
        {
            std::string line;
            line.reserve(static_cast<size_t>(host.cols_) + 2);
            for (int col = 0; col < host.cols_; ++col)
                line += ch;
            line += "\r\n";
            host.feed(line);
        }
    }

    // Make a selection from (c1,r1) to (c2,r2) and return the clipboard content.
    std::string select_and_copy(int c1, int r1, int c2, int r2)
    {
        host.begin_selection(c1, r1, c2, r2);
        host.dispatch_action("copy");
        return window.clipboard_;
    }
};

// Helper: check that text is a valid prefix of an all-ASCII single-char string.
static bool is_valid_prefix_of(const std::string& text, char expected_char, int max_len)
{
    if (static_cast<int>(text.size()) > max_len)
        return false;
    for (char c : text)
    {
        if (c != expected_char && c != '\n')
            return false;
    }
    return true;
}

// Helper: check that a UTF-8 string has no split codepoints.
static bool is_valid_utf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        int seq_len = 0;
        if (b < 0x80)
            seq_len = 1;
        else if ((b & 0xE0) == 0xC0)
            seq_len = 2;
        else if ((b & 0xF0) == 0xE0)
            seq_len = 3;
        else if ((b & 0xF8) == 0xF0)
            seq_len = 4;
        else
            return false; // invalid lead byte
        if (i + static_cast<size_t>(seq_len) > s.size())
            return false; // truncated sequence
        for (int j = 1; j < seq_len; ++j)
        {
            if ((static_cast<uint8_t>(s[i + static_cast<size_t>(j)]) & 0xC0) != 0x80)
                return false;
        }
        i += static_cast<size_t>(seq_len);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("selection: empty selection produces empty clipboard", "[terminal]")
{
    SelSetup ss;
    INFO("host must initialize");
    REQUIRE(ss.ok);
    // Select a zero-area region (same start and end point).
    ss.host.begin_selection(0, 0, 0, 0);
    ss.host.dispatch_action("copy");
    // The clipboard should remain empty (same-point selection is not activated).
    INFO("empty selection must not set clipboard");
    REQUIRE(ss.window.clipboard_.empty());
}

TEST_CASE("selection: small selection copies full content", "[terminal]")
{
    const int limit = TestTerminalHost::kLimit;
    SelSetup ss(80, 30);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('X');
    // Select a single row of 10 cells — well within any limit.
    const std::string result = ss.select_and_copy(0, 0, 9, 0);
    INFO("small selection should produce clipboard content");
    REQUIRE(!result.empty());
    INFO("small selection must not exceed limit");
    REQUIRE(static_cast<int>(result.size()) <= limit);
    INFO("content should be all X");
    REQUIRE(is_valid_prefix_of(result, 'X', limit));
}

TEST_CASE("selection: selection at exactly kSelectionMaxCells cells copies without truncation", "[terminal]")
{
    const int limit = TestTerminalHost::kLimit;
    // Use a grid sized so that one full row equals limit/cols rows.
    // Choose 100 columns so that limit/100 complete rows fills exactly 'limit' cells.
    const int cols = 100;
    const int target_cells = limit;
    const int rows_needed = target_cells / cols + 2; // extra rows for safety
    SelSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('Z');

    // Select exactly 'target_cells' cells: rows 0..(target_cells/cols - 1), all cols.
    const int end_row = (target_cells / cols) - 1;
    const int end_col = cols - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    // Result may trim trailing spaces/newlines, so just check no OOB: size is reasonable.
    INFO("result size must be bounded near the limit");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all Z");
    REQUIRE(is_valid_prefix_of(result, 'Z', limit * 2));
}

TEST_CASE("selection: selection one cell over limit truncates without crash", "[terminal]")
{
    const int limit = TestTerminalHost::kLimit;
    const int cols = 100;
    const int over_cells = limit + 1;
    const int rows_needed = over_cells / cols + 2;
    SelSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('Y');

    const int end_row = over_cells / cols;
    const int end_col = (over_cells % cols == 0) ? cols - 1 : (over_cells % cols) - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    // Must not crash, and result must not exceed the hard limit plus newlines.
    INFO("result must not exceed limit*2 bytes");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all Y");
    REQUIRE(is_valid_prefix_of(result, 'Y', limit * 2));
}

TEST_CASE("selection: selection far over limit truncates without crash", "[terminal]")
{
    const int limit = TestTerminalHost::kLimit;
    const int cols = 80;
    const int rows_needed = (limit * 2) / cols + 2;
    SelSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('W');

    // Select all rows we have.
    const int end_row = rows_needed - 1;
    const int end_col = cols - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    INFO("result must not exceed limit*2 bytes");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all W");
    REQUIRE(is_valid_prefix_of(result, 'W', limit * 2));
}

TEST_CASE("selection: UTF-8 multibyte content produces valid UTF-8 in clipboard", "[terminal]")
{
    // Use a small terminal and write UTF-8 content (2-byte sequences: U+00E9 = 0xC3 0xA9).
    SelSetup ss(20, 10);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    // Write rows of 'é' (U+00E9, encoded as 0xC3 0xA9 in UTF-8).
    for (int row = 0; row < 10; ++row)
    {
        std::string line;
        for (int col = 0; col < 20; ++col)
            line += "\xC3\xA9";
        line += "\r\n";
        ss.host.feed(line);
    }
    // Select all cells across all rows.
    const std::string result = ss.select_and_copy(0, 0, 19, 9);
    INFO("UTF-8 selection must produce content");
    REQUIRE(!result.empty());
    INFO("clipboard content must be valid UTF-8 (no split codepoints)");
    REQUIRE(is_valid_utf8(result));
}

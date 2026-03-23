#include "support/fake_renderer.h"
#include "support/fake_window.h"

#include <draxul/terminal_host_base.h>

#include <catch2/catch_all.hpp>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// TestableTerminalHost — same pattern as terminal_vt_tests.cpp.
// Exposes feed() and cell_hl() so tests can drive SGR sequences and observe
// the resulting hl_attr_ids assigned to grid cells.
// ---------------------------------------------------------------------------

namespace
{

class AttrIdTestHost final : public TerminalHostBase
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    uint16_t cell_hl(int col, int row)
    {
        return grid().get_cell(col, row).hl_attr_id;
    }

    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "attr-id-test";
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

    bool do_process_write(std::string_view) override
    {
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
};

// ---------------------------------------------------------------------------
// Setup helper — mirrors TermSetup in terminal_vt_tests.cpp.
// ---------------------------------------------------------------------------

struct AttrSetup
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    AttrIdTestHost host;
    bool ok = false;

    explicit AttrSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        TextServiceConfig ts_cfg;
        ts_cfg.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts"
            / "JetBrainsMonoNerdFont-Regular.ttf")
                               .string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{ window, renderer, text_service, {}, vp, 96.0f };

        HostCallbacks cbs;
        cbs.request_frame = [] {};
        cbs.request_quit = [] {};
        cbs.wake_window = [] {};
        cbs.set_window_title = [](const std::string&) {};
        cbs.set_text_input_area = [](int, int, int, int) {};

        ok = host.initialize(ctx, std::move(cbs));
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// -----------------------------------------------------------------------
// Test 1: Same SGR sequence produces the same hl_attr_id on two cells.
// We write a red foreground char, move cursor, write another red char,
// and assert both cells share the same non-zero attr id.
// -----------------------------------------------------------------------
TEST_CASE("attr_id: identical SGR produces same hl_attr_id", "[grid]")
{
    AttrSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // ESC[31m = red foreground (ANSI color 1).
    ts.host.feed("\x1B[31mA");
    // Reset position to col 2, same row, with same SGR active.
    ts.host.feed("\x1B[31mB");

    const uint16_t id_A = ts.host.cell_hl(0, 0);
    const uint16_t id_B = ts.host.cell_hl(1, 0);

    INFO("non-default attr must have non-zero id");
    REQUIRE(id_A != 0);
    INFO("identical SGR must yield the same hl_attr_id");
    REQUIRE(id_A == id_B);
}

// -----------------------------------------------------------------------
// Test 2: Five distinct SGR sequences produce five distinct hl_attr_ids.
// -----------------------------------------------------------------------
TEST_CASE("attr_id: distinct SGR sequences produce distinct hl_attr_ids", "[grid]")
{
    AttrSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // 5 distinct ANSI foreground colors written to 5 consecutive cells.
    // Codes: 31=red, 32=green, 33=yellow, 34=blue, 35=magenta.
    ts.host.feed("\x1B[31mA\x1B[32mB\x1B[33mC\x1B[34mD\x1B[35mE");

    std::set<uint16_t> ids;
    for (int col = 0; col < 5; ++col)
        ids.insert(ts.host.cell_hl(col, 0));

    INFO("five distinct foreground colors must produce five distinct attr ids");
    REQUIRE(static_cast<int>(ids.size()) == 5);
}

// -----------------------------------------------------------------------
// Test 3: Cache does not grow with duplicate inputs.
// Write the same red-foreground character 100 times across a 20-col grid
// (wrapping to row 1 etc.) then apply a brand-new color and verify its id
// is exactly 2 (i.e. only two distinct ids have ever been assigned).
// -----------------------------------------------------------------------
TEST_CASE("attr_id: cache does not grow with duplicate attributes", "[grid]")
{
    AttrSetup ts(20, 10);
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // Write 100 red chars, staying within the 20×10 = 200 cell grid.
    std::string seq;
    seq.reserve(8 + 100);
    seq += "\x1B[31m"; // set red fg once
    for (int i = 0; i < 100; ++i)
        seq += 'A';
    ts.host.feed(seq);

    // Move to a known position (row 0, col 0) and write with a fresh color.
    ts.host.feed("\x1B[H"); // CUP: move to (1,1) = row 0 col 0 in 0-based
    ts.host.feed("\x1B[32mZ"); // green foreground at (0,0)

    const uint16_t id_green = ts.host.cell_hl(0, 0);

    // Only two distinct non-zero ids should have been allocated:
    // id 1 for red, id 2 for green.
    INFO("after 100 identical red attrs, the next new attr must get id 2, not 101");
    REQUIRE(id_green == 2);
}

// -----------------------------------------------------------------------
// Test 4: 100 distinct attributes produce 100 distinct ids, and a second
// pass over the same attributes returns the same ids (stable mapping).
// -----------------------------------------------------------------------
TEST_CASE("attr_id: 100 distinct attrs → 100 distinct stable ids", "[grid]")
{
    // Use a large grid so 100 cells fit without scrolling.
    AttrSetup ts(100, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // Build a sequence: set fg to xterm color index i (using ESC[38;5;<i>m)
    // and write one character per color.  Colors 16..115 are all distinct.
    std::string seq;
    seq.reserve(100 * 12);
    for (int i = 0; i < 100; ++i)
    {
        // ESC[38;5;<16+i>m sets fg to xterm palette color 16+i.
        seq += "\x1B[38;5;" + std::to_string(16 + i) + "mA";
    }
    ts.host.feed(seq);

    // Collect first-pass ids.
    std::vector<uint16_t> first_pass(100);
    for (int col = 0; col < 100; ++col)
        first_pass[col] = ts.host.cell_hl(col, 0);

    // Verify uniqueness.
    const std::set<uint16_t> unique_ids(first_pass.begin(), first_pass.end());
    INFO("100 distinct xterm colors must produce 100 distinct attr ids");
    REQUIRE(static_cast<int>(unique_ids.size()) == 100);

    // Second pass: move cursor back to (0,0) and replay the same colors.
    ts.host.feed("\x1B[H");
    std::string seq2;
    seq2.reserve(100 * 12);
    for (int i = 0; i < 100; ++i)
        seq2 += "\x1B[38;5;" + std::to_string(16 + i) + "mB";
    ts.host.feed(seq2);

    // The ids must be identical to first_pass (stable mapping).
    for (int col = 0; col < 100; ++col)
    {
        const uint16_t id2 = ts.host.cell_hl(col, 0);
        INFO("attr id must be stable across repeated lookups");
        REQUIRE(id2 == first_pass[col]);
    }
}

// -----------------------------------------------------------------------
// Test 5: Default/zero HlAttr (no SGR set) returns id 0 (the sentinel
// for "no highlight") and is stable across multiple calls.
// -----------------------------------------------------------------------
TEST_CASE("attr_id: default attr (no SGR) is stable and returns id 0", "[grid]")
{
    AttrSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // Write two chars without any SGR — both should use the default attr (id 0).
    ts.host.feed("AB");

    const uint16_t id_A = ts.host.cell_hl(0, 0);
    const uint16_t id_B = ts.host.cell_hl(1, 0);

    // The implementation returns 0 for a fully-default HlAttr.
    INFO("default attr must return id 0 (sentinel for no highlight)");
    REQUIRE(id_A == static_cast<uint16_t>(0));
    INFO("default attr id must be stable");
    REQUIRE(id_A == id_B);

    // Write a third default-attr char and confirm id is still 0.
    ts.host.feed("C");
    const uint16_t id_C = ts.host.cell_hl(2, 0);
    INFO("default attr must always return id 0");
    REQUIRE(id_C == static_cast<uint16_t>(0));
}

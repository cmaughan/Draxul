// shell_host_crash_tests.cpp
//
// Tests for shell host process crash lifecycle behaviour.
//
// Production shell processes are server-owned and covered by server runtime
// tests. These low-level adapter tests exercise TerminalHostBase with a
// controllable fake process that exits mid-session.
//
// The tests verify:
//   1. is_running() returns false once the fake process signals exit.
//   2. Writing input after process exit does not crash or hang.
//   3. Grid cells are not mutated after the process has exited.
//
// Server integration fixtures provide the corresponding real-process coverage.

#include <catch2/catch_test_macros.hpp>

#include <draxul/local_terminal_host.h>
#include <draxul/terminal_host_base.h>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
#include <draxul/host.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace draxul;

// ---------------------------------------------------------------------------
// Minimal fakes
// ---------------------------------------------------------------------------

namespace
{

using ShCrashFakeWindow = draxul::tests::FakeWindow;
using ShCrashFakeRenderer = draxul::tests::FakeTermRenderer;

// ---------------------------------------------------------------------------
// FakeShellHost — simulates a shell host whose process can be marked as
// "exited" on demand.  This mirrors what happens when bash/zsh terminates
// unexpectedly: do_process_is_running() returns false and
// do_process_write() returns false (pipe is closed).
//
// Public wrapper methods (write_input, is_process_running) are provided
// so tests can invoke the protected virtuals through the concrete subclass
// without breaking the TerminalHostBase encapsulation boundary.
// ---------------------------------------------------------------------------

class FakeShellHost final : public TerminalHostBase
{
public:
    // Simulate the shell process exiting.
    void simulate_process_exit()
    {
        process_alive_ = false;
    }

    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    // Public wrappers around the protected virtual back-end.
    bool write_input(std::string_view text)
    {
        return do_process_write(text);
    }
    bool is_process_running() const
    {
        return do_process_is_running();
    }

    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }

    int write_count = 0;
    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "fake-shell";
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
        if (!process_alive_)
            return false; // pipe is closed after process exit
        ++write_count;
        return true;
    }
    std::vector<std::string> do_process_drain() override
    {
        return {};
    }
    bool do_process_resize(int, int) override
    {
        return process_alive_;
    }
    bool do_process_is_running() const override
    {
        return process_alive_;
    }
    void do_process_shutdown() override
    {
        process_alive_ = false;
    }

private:
    bool process_alive_ = true;
};

class FakeLocalShellHost final : public LocalTerminalHost
{
public:
    void simulate_process_exit()
    {
        process_alive_ = false;
    }

    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }

    int cols_ = 20;
    int rows_ = 5;
    int resize_count = 0;

protected:
    std::string_view host_name() const override
    {
        return "fake-local-shell";
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
        return process_alive_;
    }
    std::vector<std::string> do_process_drain() override
    {
        return {};
    }
    bool do_process_resize(int, int) override
    {
        ++resize_count;
        return true;
    }
    bool do_process_is_running() const override
    {
        return process_alive_;
    }
    void do_process_shutdown() override
    {
        process_alive_ = false;
    }

private:
    bool process_alive_ = true;
};

struct ShCrashSetup
{
    ShCrashFakeWindow window;
    ShCrashFakeRenderer renderer;
    TextService text_service;
    FakeShellHost host;
    draxul::tests::TestHostCallbacks callbacks;
    bool ok = false;

    explicit ShCrashSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        draxul::tests::init_text_service(text_service);

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = vp,
        };
        ok = host.initialize(ctx, callbacks);
    }
};

struct LocalResizeSetup
{
    ShCrashFakeWindow window;
    ShCrashFakeRenderer renderer;
    TextService text_service;
    FakeLocalShellHost host;
    draxul::tests::TestHostCallbacks callbacks;
    bool ok = false;

    explicit LocalResizeSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        draxul::tests::init_text_service(text_service);

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = vp,
        };
        ok = host.initialize(ctx, callbacks);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("shell host: is_running returns false after process exits", "[shell_host]")
{
    // A real ShellHost spawns bash/zsh via UnixPtyProcess (macOS/Linux) or
    // ConptyProcess (Windows).  The ShellHost class lives in an anonymous
    // namespace and is not directly instantiable from tests; we exercise the
    // identical TerminalHostBase::is_running() contract via a controllable
    // fake instead.
    ShCrashSetup ts;
    REQUIRE(ts.ok);

    // Process is alive initially.
    REQUIRE(ts.host.is_running());

    // Simulate unexpected process exit (e.g. the shell crashes or the user
    // types `exit`).
    ts.host.simulate_process_exit();

    // After exit, is_running() must return false.
    REQUIRE_FALSE(ts.host.is_running());
}

TEST_CASE("shell host: write after process exit does not crash", "[shell_host]")
{
    // After the shell process exits, any attempt to write input (e.g. the
    // user keeps typing) must not crash or hang.  write_input() (which
    // delegates to do_process_write()) should return false gracefully when
    // the pipe is closed.
    ShCrashSetup ts;
    REQUIRE(ts.ok);

    ts.host.simulate_process_exit();

    // Writing after exit must not throw or crash — it returns false.
    bool result = ts.host.write_input("hello\r");
    REQUIRE_FALSE(result); // false = write failed, pipe closed
}

TEST_CASE("shell host: grid is not mutated after process exits", "[shell_host]")
{
    // After the shell process exits, the host should not mutate the grid with
    // new output (there is none).  We record a cell value before the simulated
    // crash and verify it is unchanged afterwards.
    ShCrashSetup ts;
    REQUIRE(ts.ok);

    // Write some content so the grid has known text.
    ts.host.feed("Hello");
    std::string cell_before = ts.host.cell_text(0, 0);

    // Simulate crash.
    ts.host.simulate_process_exit();

    // No new output arrives after the process exits (drain returns nothing).
    // The grid must still show the last content, unchanged.
    std::string cell_after = ts.host.cell_text(0, 0);
    REQUIRE(cell_before == cell_after);

    // The process is no longer running.
    REQUIRE_FALSE(ts.host.is_running());
}

TEST_CASE("local shell: status text shows exited after process exit", "[shell_host]")
{
    LocalResizeSetup ts;
    REQUIRE(ts.ok);

    CHECK(ts.host.status_text().find("[exited]") == std::string::npos);

    ts.host.simulate_process_exit();

    CHECK(ts.host.status_text().find("[exited]") != std::string::npos);
}

TEST_CASE("local shell: viewport resize preserves visible text", "[shell_host]")
{
    LocalResizeSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("ABCD\r\nEFGH\r\nIJKL");
    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(3, 2) == "L");

    HostViewport resized;
    resized.grid_size.x = 4;
    resized.grid_size.y = 4;
    ts.host.set_viewport(resized);

    INFO("existing visible rows remain after resize-triggered grid reset");
    REQUIRE(ts.host.cell_text(0, 1) == "A");
    REQUIRE(ts.host.cell_text(3, 3) == "L");
    INFO("the backing process still receives the resize notification");
    REQUIRE(ts.host.resize_count >= 1);
}

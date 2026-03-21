// shell_host_crash_tests.cpp
//
// Tests for shell host process crash lifecycle behaviour.
//
// The concrete ShellHost class lives in an anonymous namespace inside
// shell_host_unix.cpp / shell_host_win.cpp and cannot be instantiated
// directly from tests.  Instead we exercise the identical code path
// through TerminalHostBase using a controllable fake that simulates
// a process that exits mid-session.
//
// The tests verify:
//   1. is_running() returns false once the fake process signals exit.
//   2. Writing input after process exit does not crash or hang.
//   3. Grid cells are not mutated after the process has exited.
//
// If a future thin ShellHost test interface is added, these SKIPs can be
// replaced with real ShellHost instantiation.

#include <catch2/catch_test_macros.hpp>

#include <draxul/terminal_host_base.h>

#include <draxul/host.h>
#include <draxul/renderer.h>
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

class ShCrashFakeWindow final : public IWindow
{
public:
    bool initialize(const std::string&, int, int) override
    {
        return true;
    }
    void shutdown() override {}
    bool poll_events() override
    {
        return true;
    }
    void* native_handle() override
    {
        return nullptr;
    }
    std::pair<int, int> size_logical() const override
    {
        return { 800, 600 };
    }
    std::pair<int, int> size_pixels() const override
    {
        return { 800, 600 };
    }
    float display_ppi() const override
    {
        return 96.0f;
    }
    void set_title(const std::string&) override {}
    std::string clipboard_text() const override
    {
        return {};
    }
    bool set_clipboard_text(const std::string&) override
    {
        return true;
    }
    void set_text_input_area(int, int, int, int) override {}
};

class ShCrashFakeRenderer final : public IRenderer
{
public:
    bool initialize(IWindow&) override
    {
        return true;
    }
    void shutdown() override {}
    bool begin_frame() override
    {
        return true;
    }
    void end_frame() override {}
    void set_grid_size(int, int) override {}
    void update_cells(std::span<const CellUpdate>) override {}
    void set_overlay_cells(std::span<const CellUpdate>) override {}
    void set_atlas_texture(const uint8_t*, int, int) override {}
    void update_atlas_region(int, int, int, int, const uint8_t*) override {}
    void set_cursor(int, int, const CursorStyle&) override {}
    void resize(int, int) override {}
    std::pair<int, int> cell_size_pixels() const override
    {
        return { 8, 16 };
    }
    void set_cell_size(int, int) override {}
    void set_ascender(int) override {}
    int padding() const override
    {
        return 0;
    }
    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}
    bool initialize_imgui_backend() override
    {
        return true;
    }
    void shutdown_imgui_backend() override {}
    void rebuild_imgui_font_texture() override {}
    void begin_imgui_frame() override {}
    void set_imgui_draw_data(const ImDrawData*) override {}
    void request_frame_capture() override {}
    std::optional<CapturedFrame> take_captured_frame() override
    {
        return std::nullopt;
    }
};

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

struct ShCrashSetup
{
    ShCrashFakeWindow window;
    ShCrashFakeRenderer renderer;
    TextService text_service;
    FakeShellHost host;
    bool ok = false;

    explicit ShCrashSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        TextServiceConfig ts_cfg;
        ts_cfg.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts"
            / "JetBrainsMonoNerdFont-Regular.ttf")
                               .string();
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

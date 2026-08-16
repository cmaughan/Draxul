// Local/remote terminal-surface parity (audit bug #12).
//
// LocalTerminalHost and RemoteTerminalHost share TerminalSurfaceHostBase for
// selection, copy-on-select, copy mode, and wheel handling. These tests drive
// both hosts through identical event sequences over identical grid content and
// assert identical user-visible behaviour:
//   - drag selection with copy_on_select copies the same text,
//   - copy-mode key navigation yanks the same text, and key *releases* are
//     swallowed (the guard the remote host historically lacked),
//   - text input while copy mode is active never reaches the process,
//   - a wheel scroll over scrollback clears the selection on both hosts, so a
//     later copy cannot grab text the user never selected (the behaviour the
//     local host historically lacked).

#include "support/temp_dir.h"
#include "support/test_local_terminal_host.h"
#include "support/test_support.h"
#include "support/test_terminal_host_fixture.h"

#include <draxul/control_plane.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>

#include <SDL3/SDL_keycode.h>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

constexpr int kCols = 20;
constexpr int kRows = 5;
constexpr int kCellW = 8; // FakeTermRenderer::cell_size_pixels()
constexpr int kCellH = 16;

KeyEvent key_event(int keycode, bool pressed, uint16_t mod = 0)
{
    KeyEvent event;
    event.keycode = keycode;
    event.mod = mod;
    event.pressed = pressed;
    return event;
}

MouseButtonEvent button_event(int button, bool pressed, int col, int row,
    int clicks = 1)
{
    MouseButtonEvent event;
    event.button = button;
    event.pressed = pressed;
    event.pos = { col * kCellW, row * kCellH };
    event.clicks = clicks;
    return event;
}

MouseMoveEvent move_event(int col, int row)
{
    MouseMoveEvent event;
    event.pos = { col * kCellW, row * kCellH };
    return event;
}

MouseWheelEvent wheel_event(float dy)
{
    MouseWheelEvent event;
    event.pos = { 0, 0 };
    event.delta = { 0.0f, dy };
    return event;
}

// The identical event script both hosts run; observable results are collected
// so the local and remote transcripts can be compared field by field.
struct SurfaceObservations
{
    std::string drag_copy_clipboard;
    std::string copy_mode_yank_clipboard;
    std::string clipboard_after_wheel_copy;

    bool operator==(const SurfaceObservations&) const = default;
};

// Runs the shared script against a host. `set_clipboard` / `get_clipboard`
// abstract the FakeWindow, `settle` lets the remote host pump between steps.
template <typename HostT, typename Settle>
SurfaceObservations run_surface_script(HostT& host, FakeWindow& window,
    Settle&& settle)
{
    SurfaceObservations result;

    // 1. Drag selection over "hello" with copy_on_select enabled.
    window.clipboard_.clear();
    host.on_mouse_button(button_event(1, true, 0, 0));
    host.on_mouse_move(move_event(4, 0));
    host.on_mouse_button(button_event(1, false, 4, 0));
    result.drag_copy_clipboard = window.clipboard_;

    // 2. Copy mode: navigate to (0,0), select three cells, with a key RELEASE
    // in the middle that must be swallowed rather than moving the cursor.
    window.clipboard_.clear();
    REQUIRE(host.dispatch_action("toggle_copy_mode"));
    host.on_key(key_event(SDLK_G, true)); // top row
    host.on_key(key_event(SDLK_0, true)); // first column
    host.on_key(key_event(SDLK_V, true)); // start selecting
    host.on_key(key_event(SDLK_L, true)); // -> (1,0)
    host.on_key(key_event(SDLK_L, true)); // -> (2,0)
    host.on_key(key_event(SDLK_L, false)); // release: must NOT move
    // Text input paired with copy-mode keys must be swallowed, not typed.
    host.on_text_input({ .text = "l" });
    host.on_key(key_event(SDLK_Y, true)); // yank "hel"
    result.copy_mode_yank_clipboard = window.clipboard_;
    settle();

    // 3. Wheel over scrollback clears the selection: a fresh selection is
    // made, the wheel scrolls, and a subsequent copy must find nothing.
    host.on_mouse_button(button_event(1, true, 0, 1));
    host.on_mouse_move(move_event(5, 1));
    host.on_mouse_button(button_event(1, false, 5, 1));
    window.clipboard_ = "sentinel";
    host.on_mouse_wheel(wheel_event(1.0f));
    settle();
    REQUIRE(host.dispatch_action("copy"));
    result.clipboard_after_wheel_copy = window.clipboard_;

    return result;
}

// --- Remote fixture: fake control server publishing a fixed snapshot --------

struct RemoteSurfaceFixture
{
    TempDir temp{ "draxul-surface-parity" };
    ControlServer control;
    std::jthread dispatcher;
    std::atomic<int> input_calls = 0;

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    RemoteTerminalHost host{ {
        .runtime_directory = temp.path,
        .client_id = "parity-client",
        .server_epoch = "parity-epoch",
    } };
    bool ok = false;

    explicit RemoteSurfaceFixture(std::vector<std::string> rows)
    {
        std::string control_error;
        REQUIRE(control.start(
            namespaced_control_id(kServerControlId, temp.path),
            temp.path, [] {}, &control_error));

        TerminalSemanticSnapshot snapshot;
        snapshot.cols = kCols;
        snapshot.rows = kRows;
        snapshot.metadata.cursor.col = 0;
        snapshot.metadata.cursor.row = 0;
        snapshot.metadata.cursor.visible = true;
        snapshot.cells.resize(static_cast<size_t>(kCols) * kRows,
            TerminalCellSnapshot{ .text = " " });
        for (size_t row = 0; row < rows.size(); ++row)
        {
            for (size_t col = 0; col < rows[row].size() && col < kCols; ++col)
            {
                snapshot.cells[row * kCols + col].text
                    = std::string(1, rows[row][col]);
            }
        }

        RemoteTerminalAttach attach{
            .pane = {
                .pane_id = std::string(kFakeRemotePaneId),
                .terminal_id = std::string(kFakeRemoteTerminalId),
                .name = "Surface Parity",
                .execution_domain = "server_terminal",
            },
            .state = {
                .kind = RemoteTerminalEventKind::Snapshot,
                .version = {
                    .server_epoch = "parity-epoch",
                    .terminal_id = std::string(kFakeRemoteTerminalId),
                    .generation = 1,
                },
                .controller_client_id = "parity-client",
                .snapshot = snapshot,
            },
        };

        dispatcher = std::jthread([this, attach](std::stop_token stop) {
            while (!stop.stop_requested())
            {
                control.process_pending([&](const ControlRequest& request) {
                    if (request.method == "fake.attach")
                    {
                        return ControlMethodResult::success(
                            remote_terminal_attach_to_json(attach));
                    }
                    if (request.method == "fake.poll")
                    {
                        return ControlMethodResult::success({
                            { "events", nlohmann::json::array() },
                        });
                    }
                    if (request.method == "fake.input")
                        ++input_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        init_text_service(text_service);
        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .launch_options = {
                .kind = HostKind::RemoteTerminal,
                .copy_on_select = true,
            },
            .initial_viewport = {
                .pixel_size = { kCols * kCellW, kRows * kCellH },
                .grid_size = { kCols, kRows },
            },
            .display_ppi = 96.0f,
        };
        ok = host.initialize(context, callbacks);
        if (ok)
        {
            ok = pump_until([this] { host.pump(); },
                [this] {
                    return host.status_text().find("remote controller")
                        != std::string::npos;
                });
        }
    }

    ~RemoteSurfaceFixture()
    {
        host.shutdown();
        dispatcher.request_stop();
        dispatcher.join();
        control.stop();
    }
};

} // namespace

TEST_CASE("terminal surface: local and remote hosts behave identically",
    "[host][terminal][selection][parity]")
{
    // Local host with the same content and copy_on_select enabled.
    HostLaunchOptions local_options;
    local_options.copy_on_select = true;
    TerminalHostFixture<TestLocalTerminalHost> local(kCols, kRows,
        local_options);
    REQUIRE(local.ok);
    local.host.feed("hello world\r\nsecond line");

    RemoteSurfaceFixture remote({ "hello world", "second line" });
    REQUIRE(remote.ok);

    const SurfaceObservations local_result = run_surface_script(
        local.host, local.window, [] {});
    const SurfaceObservations remote_result = run_surface_script(
        remote.host, remote.window, [&] {
            // Give the worker a moment to absorb queued scroll commands; the
            // fake server publishes no new state, so the grid stays put.
            remote.host.pump();
        });

    // Field-by-field, the surfaces must agree.
    CHECK(local_result.drag_copy_clipboard
        == remote_result.drag_copy_clipboard);
    CHECK(local_result.copy_mode_yank_clipboard
        == remote_result.copy_mode_yank_clipboard);
    CHECK(local_result.clipboard_after_wheel_copy
        == remote_result.clipboard_after_wheel_copy);

    // And agree on the *right* answers:
    // copy_on_select drag over cells (0,0)-(4,0) copies "hello".
    CHECK(local_result.drag_copy_clipboard == "hello");
    // copy-mode yank spans exactly three cells — the release of 'l' must not
    // have advanced the cursor a third time (bug #12's missing guard).
    CHECK(local_result.copy_mode_yank_clipboard == "hel");
    // the wheel cleared the selection, so the copy action found nothing and
    // the clipboard kept its sentinel (bug #12's wheel drift, now canonical
    // on both hosts).
    CHECK(local_result.clipboard_after_wheel_copy == "sentinel");

    // Copy-mode text input must never have reached either process.
    CHECK(local.host.written.find('l') == std::string::npos);
    CHECK(remote.input_calls.load() == 0);
}

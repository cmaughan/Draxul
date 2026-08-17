#include <catch2/catch_test_macros.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#include <draxul/text_service.h>
#include <draxul/topology_client.h>

#include <SDL3/SDL_keycode.h>

#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class ServerRunGuard
{
public:
    explicit ServerRunGuard(ServerKernel& server)
        : server_(server)
        , thread_([&server] { server.run_until_stopped(); })
    {
    }

    ~ServerRunGuard()
    {
        server_.request_stop();
    }

private:
    ServerKernel& server_;
    std::jthread thread_;
};

template <typename Predicate>
bool pump_until(RemoteTerminalHost& host, Predicate predicate)
{
    return draxul::tests::pump_until([&host] { host.pump(); },
        std::move(predicate), std::chrono::seconds(3));
}

bool rename_with_retry(const std::filesystem::path& from,
    const std::filesystem::path& to)
{
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::error_code error;
    do
    {
        error.clear();
        std::filesystem::rename(from, to, error);
        if (!error)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void pump_for(RemoteTerminalHost& first, RemoteTerminalHost& second,
    std::chrono::steady_clock::duration duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
    {
        first.pump();
        second.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool wait_for_scrollback(RemoteTerminalClient& client, uint64_t minimum_rows,
    std::string& error)
{
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        bool changed = false;
        if (!client.poll(changed, error))
            return false;
        RemoteTerminalScrollbackPage page;
        if (!client.read_scrollback(1, 1, page, error))
            return false;
        if (page.total_rows >= minimum_rows)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    error = "Timed out waiting for server-owned scrollback.";
    return false;
}

bool snapshot_contains(
    const TerminalSemanticSnapshot& snapshot, std::string_view needle)
{
    std::string text;
    for (const auto& cell : snapshot.cells)
        text += cell.text;
    return text.find(needle) != std::string::npos;
}

TerminalSemanticSnapshot snapshot_from_rows(
    std::initializer_list<std::string_view> rows,
    std::string title = {}, int cursor_col = 0, int cursor_row = 0)
{
    if (rows.size() == 0)
        return {};
    const int cols = static_cast<int>(rows.begin()->size());
    TerminalSemanticSnapshot snapshot{
        .cols = cols,
        .rows = static_cast<int>(rows.size()),
        .metadata = {
            .cursor = {
                .col = cursor_col,
                .row = cursor_row,
                .visible = true,
            },
            .title = std::move(title),
        },
    };
    snapshot.cells.reserve(
        static_cast<size_t>(snapshot.cols) * snapshot.rows);
    for (const std::string_view row : rows)
    {
        if (static_cast<int>(row.size()) != cols)
            return {};
        for (const char ch : row)
        {
            snapshot.cells.push_back({
                .text = std::string(1, ch),
            });
        }
    }
    return snapshot;
}

class RecordingHostCallbacks final : public IHostCallbacks
{
public:
    void request_frame() override
    {
        ++request_frame_calls;
    }
    void request_quit() override {}
    void wake_window() override
    {
        ++wake_window_calls;
    }
    void set_window_title(const std::string& title) override
    {
        ++window_title_calls;
        last_window_title = title;
    }
    void set_text_input_area(int, int, int, int) override {}
    void push_toast(int level, std::string_view message) override
    {
        ++push_toast_calls;
        last_toast_level = level;
        last_toast_message = std::string(message);
    }

    std::atomic<int> request_frame_calls = 0;
    std::atomic<int> wake_window_calls = 0;
    std::atomic<int> window_title_calls = 0;
    std::atomic<int> push_toast_calls = 0;
    int last_toast_level = -1;
    std::string last_toast_message;
    std::string last_window_title;
};

} // namespace

TEST_CASE("remote terminal client skips unknown additive events",
    "[client][remote-terminal][protocol]")
{
    TempDir temp("draxul-remote-unknown-event");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    const TerminalSemanticSnapshot snapshot = snapshot_from_rows({ "A" });
    const RemoteTerminalAttach attach{
        .pane = {
            .pane_id = "unknown-pane",
            .terminal_id = "unknown-terminal",
            .name = "Unknown event",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "unknown-epoch",
                .terminal_id = "unknown-terminal",
                .generation = 1,
            },
            .snapshot = snapshot,
        },
    };
    std::jthread dispatcher([&](std::stop_token stop) {
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
                        { "events", nlohmann::json::array({
                                        {
                                            { "kind", "future-decoration" },
                                            { "payload", "ignored" },
                                        },
                                    }) },
                    });
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    RemoteTerminalClient client({
        .runtime_directory = temp.path,
        .client_id = "unknown-client",
        .expected_server_epoch = "unknown-epoch",
    });
    std::string error;
    REQUIRE(client.attach(error));
    bool changed = true;
    REQUIRE(client.poll(changed, error));
    CHECK_FALSE(changed);
    CHECK(client.skipped_unknown_event_count() == 1);

    dispatcher.request_stop();
}

TEST_CASE("remote terminal host shutdown is bounded and stops between commands",
    "[host][remote-terminal][shutdown]")
{
    TempDir temp("draxul-remote-host-bounded-shutdown");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    std::mutex input_mutex;
    std::condition_variable input_changed;
    bool release_first_input = false;
    std::atomic<int> input_calls = 0;
    std::atomic<uint64_t> first_request_id = 0;
    std::atomic<uint64_t> second_request_id = 0;
    std::atomic<bool> first_input_started = false;
    std::atomic<bool> first_input_fully_framed = false;
    TerminalSemanticSnapshot snapshot{
        .cols = 80,
        .rows = 12,
        .cells = std::vector<TerminalCellSnapshot>(
            80 * 12, TerminalCellSnapshot{ .text = " " }),
        .metadata = {
            .modes = { .bracketed_paste = true },
        },
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Shutdown Test",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "bounded-shutdown-epoch",
                .terminal_id = std::string(kFakeRemoteTerminalId),
                .generation = 1,
            },
            .controller_client_id = "bounded-shutdown-client",
            .snapshot = snapshot,
        },
    };
    std::jthread dispatcher([&](std::stop_token stop) {
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
                {
                    const int call = ++input_calls;
                    if (call == 1)
                    {
                        first_request_id = request.params.value(
                            "request_id", uint64_t{ 0 });
                    }
                    else if (call == 2)
                    {
                        second_request_id = request.params.value(
                            "request_id", uint64_t{ 0 });
                    }
                    if (call == 1)
                    {
                        const std::string text
                            = request.params.value("text", "");
                        first_input_fully_framed
                            = text.size() <= 48 * 1024
                            && text.starts_with("\x1B[200~")
                            && text.ends_with("\x1B[201~");
                        {
                            std::lock_guard lock(input_mutex);
                            first_input_started = true;
                        }
                        input_changed.notify_all();
                        std::unique_lock lock(input_mutex);
                        input_changed.wait(lock,
                            [&] {
                                return release_first_input
                                    || stop.stop_requested();
                            });
                    }
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "bounded-shutdown-client",
        .server_epoch = "bounded-shutdown-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 640, 240 },
            .grid_size = { 80, 12 },
        },
        .display_ppi = 96.0f,
    };
    const bool initialized = host.initialize(context, callbacks);
    INFO(host.init_error_code() << ": " << host.init_error());
    REQUIRE(initialized);
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("remote controller")
            != std::string::npos;
    }));
    CHECK(host.display_name() == "Shutdown Test");

    window.clipboard_ = std::string(10 * 48 * 1024, 'x');
    REQUIRE(host.dispatch_action("paste"));
    {
        std::unique_lock lock(input_mutex);
        REQUIRE(input_changed.wait_for(lock, std::chrono::seconds(2),
            [&] { return first_input_started.load(); }));
    }

    const auto started_at = std::chrono::steady_clock::now();
    host.shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    CHECK(elapsed < std::chrono::seconds(1));
    const auto repeated_started_at = std::chrono::steady_clock::now();
    host.shutdown();
    CHECK(std::chrono::steady_clock::now() - repeated_started_at
        < std::chrono::milliseconds(50));

    {
        std::lock_guard lock(input_mutex);
        release_first_input = true;
    }
    input_changed.notify_all();
    const auto worker_deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (host.is_running()
        && std::chrono::steady_clock::now() < worker_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_FALSE(host.is_running());
    CHECK(input_calls == 1);
    CHECK(first_input_fully_framed);

    // Recreating a host for the same client must not reuse an id still held
    // in the server's deduplication cache.
    RemoteTerminalHost replacement({
        .runtime_directory = temp.path,
        .client_id = "bounded-shutdown-client",
        .server_epoch = "bounded-shutdown-epoch",
    });
    REQUIRE(replacement.initialize(context, callbacks));
    REQUIRE(pump_until(replacement, [&] {
        return replacement.status_text().find("remote controller")
            != std::string::npos;
    }));
    replacement.on_text_input({ .text = "replacement-input" });
    REQUIRE(pump_until(replacement, [&] {
        return input_calls.load() >= 2;
    }));
    REQUIRE(first_request_id != 0);
    REQUIRE(second_request_id != 0);
    CHECK(second_request_id != first_request_id);
    replacement.shutdown();

    dispatcher.request_stop();
    input_changed.notify_all();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote terminal host chunks a large paste without stopping",
    "[host][remote-terminal][paste][backpressure]")
{
    TempDir temp("draxul-remote-host-large-paste");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "large-paste-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "large-paste-controller",
        .server_epoch = "large-paste-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 640, 240 },
            .grid_size = { 80, 12 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos;
    }));

    RemoteTerminalClient observer({
        .runtime_directory = temp.path,
        .client_id = "large-paste-observer",
        .expected_server_epoch = "large-paste-epoch",
    });
    std::string error;
    REQUIRE(observer.attach(error));

    window.clipboard_ = std::string(200 * 1024, 'x')
        + "__LARGE_PASTE_COMPLETE__";
    REQUIRE(host.dispatch_action("paste"));
    bool received_tail = false;
    for (int attempt = 0; attempt < 1000 && !received_tail; ++attempt)
    {
        host.pump();
        bool changed = false;
        REQUIRE(observer.poll(changed, error));
        received_tail = snapshot_contains(
            observer.projection().snapshot(),
            "__LARGE_PASTE_COMPLETE__");
        std::this_thread::sleep_for(
            std::chrono::milliseconds(2));
    }
    INFO(error);
    REQUIRE(received_tail);
    CHECK(host.is_running());
    CHECK(callbacks.last_toast_message.empty());
    host.shutdown();
}

TEST_CASE("remote terminal host drops rejected commands and remains usable",
    "[host][remote-terminal][input][recovery]")
{
    TempDir temp("draxul-remote-host-rejected-input");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    const auto snapshot = snapshot_from_rows(
        { "ready   ", "        " }, "Rejected Input");
    const RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Rejected Input",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "rejected-input-epoch",
                .terminal_id = std::string(kFakeRemoteTerminalId),
                .generation = 1,
            },
            .controller_client_id = "rejected-input-client",
            .snapshot = snapshot,
        },
    };
    const std::vector<std::pair<std::string, std::string>> failures{
        { "invalid_input", "Synthetic invalid input." },
        { "backpressure", "Synthetic input backpressure." },
        { "process_write_failed", "Synthetic process write failure." },
        { "unexpected_command", "Synthetic unexpected command failure." },
    };
    std::atomic<int> input_calls = 0;
    std::mutex accepted_mutex;
    std::vector<std::string> accepted_input;
    std::jthread dispatcher([&](std::stop_token stop) {
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
                {
                    const int call = input_calls++;
                    if (call < static_cast<int>(failures.size()))
                    {
                        return ControlMethodResult::error(
                            failures[call].first,
                            failures[call].second);
                    }
                    std::lock_guard lock(accepted_mutex);
                    accepted_input.push_back(
                        request.params.value("text", ""));
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "rejected-input-client",
        .server_epoch = "rejected-input-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = { .kind = HostKind::RemoteTerminal },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 8, 2 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos;
    }));

    for (size_t index = 0; index < failures.size(); ++index)
    {
        host.on_text_input({
            .text = "rejected-" + std::to_string(index),
        });
        REQUIRE(pump_until(host, [&] {
            return input_calls.load()
                >= static_cast<int>(index + 1);
        }));
        CHECK(host.is_running());
    }

    host.on_text_input({ .text = "accepted-after-errors" });
    REQUIRE(pump_until(host, [&] {
        std::lock_guard lock(accepted_mutex);
        return accepted_input.size() == 1;
    }));
    {
        std::lock_guard lock(accepted_mutex);
        REQUIRE(accepted_input
            == std::vector<std::string>{ "accepted-after-errors" });
    }
    CHECK(host.is_running());
    CHECK(callbacks.push_toast_calls
        == static_cast<int>(failures.size()));
    host.shutdown();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote terminal host keeps large paste frames ahead of later keys",
    "[host][remote-terminal][paste][ordering]")
{
    TempDir temp("draxul-remote-host-paste-order");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    auto snapshot = snapshot_from_rows(
        { "ready   ", "        " }, "Paste Ordering");
    snapshot.metadata.modes.bracketed_paste = true;
    const RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Paste Ordering",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "paste-order-epoch",
                .terminal_id = std::string(kFakeRemoteTerminalId),
                .generation = 1,
            },
            .controller_client_id = "paste-order-client",
            .snapshot = snapshot,
        },
    };
    std::mutex input_mutex;
    std::vector<std::string> input;
    std::jthread dispatcher([&](std::stop_token stop) {
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
                {
                    std::lock_guard lock(input_mutex);
                    input.push_back(request.params.value("text", ""));
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "paste-order-client",
        .server_epoch = "paste-order-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = { .kind = HostKind::RemoteTerminal },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 8, 2 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos;
    }));

    window.clipboard_ = std::string(200 * 1024, 'p');
    REQUIRE(host.dispatch_action("paste"));
    host.on_text_input({ .text = "K" });
    REQUIRE(pump_until(host, [&] {
        std::lock_guard lock(input_mutex);
        return input.size() >= 6;
    }));

    std::string reconstructed;
    {
        std::lock_guard lock(input_mutex);
        REQUIRE(input.back() == "K");
        for (size_t index = 0; index + 1 < input.size(); ++index)
        {
            INFO(index);
            REQUIRE(input[index].size() <= 48 * 1024);
            REQUIRE(input[index].starts_with("\x1B[200~"));
            REQUIRE(input[index].ends_with("\x1B[201~"));
            reconstructed.append(
                input[index].substr(6, input[index].size() - 12));
        }
    }
    REQUIRE(reconstructed == window.clipboard_);
    CHECK(host.is_running());
    host.shutdown();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote terminal host recovers from malformed and unexpected polling",
    "[host][remote-terminal][recovery]")
{
    TempDir temp("draxul-remote-host-event-recovery");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    TerminalSemanticSnapshot snapshot{
        .cols = 20,
        .rows = 5,
        .cells = std::vector<TerminalCellSnapshot>(
            20 * 5, TerminalCellSnapshot{ .text = " " }),
    };
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Recovery Test",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = "event-recovery-epoch",
                .terminal_id = std::string(kFakeRemoteTerminalId),
                .generation = 1,
            },
            .controller_client_id = "event-recovery-client",
            .snapshot = snapshot,
        },
    };
    std::atomic<int> attach_calls = 0;
    std::atomic<int> poll_calls = 0;
    std::atomic<int> input_calls = 0;
    std::atomic<int> scrollback_calls = 0;
    std::mutex accepted_mutex;
    std::vector<std::string> accepted_input;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            control.process_pending([&](const ControlRequest& request) {
                if (request.method == "fake.attach")
                {
                    ++attach_calls;
                    return ControlMethodResult::success(
                        remote_terminal_attach_to_json(attach));
                }
                if (request.method == "fake.poll")
                {
                    const int call = ++poll_calls;
                    if (call == 1)
                    {
                        return ControlMethodResult::success({
                            { "events", nlohmann::json::array({ { { "kind", "delta" } } }) },
                        });
                    }
                    if (call == 2)
                    {
                        return ControlMethodResult::error(
                            "unexpected_poll",
                            "Synthetic unexpected poll failure.");
                    }
                    return ControlMethodResult::success({
                        { "events", nlohmann::json::array() },
                    });
                }
                if (request.method == "fake.input")
                {
                    if (++input_calls == 1)
                    {
                        return ControlMethodResult::error(
                            "io_error", "Synthetic transport interruption.");
                    }
                    std::lock_guard lock(accepted_mutex);
                    accepted_input.push_back(
                        request.params.value("text", ""));
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                if (request.method == "fake.scrollback")
                {
                    ++scrollback_calls;
                    return ControlMethodResult::error(
                        "unexpected_scrollback",
                        "Synthetic unexpected scrollback failure.");
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    });

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "event-recovery-client",
        .server_epoch = "event-recovery-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = { .kind = HostKind::RemoteTerminal },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return attach_calls.load() >= 3;
    }));
    CHECK(host.is_running());

    host.on_text_input({ .text = "first" });
    REQUIRE(pump_until(host, [&] {
        return input_calls.load() >= 1;
    }));
    host.on_text_input({ .text = "second" });
    REQUIRE(pump_until(host, [&] {
        std::lock_guard lock(accepted_mutex);
        return accepted_input.size() == 2;
    }));
    {
        std::lock_guard lock(accepted_mutex);
        CHECK(accepted_input
            == std::vector<std::string>{ "first", "second" });
    }
    host.on_mouse_wheel({ .delta = { 0.0f, 1.0f } });
    REQUIRE(pump_until(host, [&] {
        return scrollback_calls.load() >= 1;
    }));
    CHECK(host.is_running());
    host.shutdown();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote terminal hosts recover in place after a long server restart",
    "[host][remote-terminal][recovery][server-restart]")
{
    TempDir temp("draxul-remote-host-server-restart");
    auto recovery
        = std::make_shared<ClientRecoveryState>(
            "restart-client");
    recovery->set_server_epoch("restart-first");

    FakeWindow window;
    FakeTermRenderer first_renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks first_callbacks;
    HostContext first_context{
        .window = &window,
        .grid_renderer = &first_renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    auto first_host
        = std::make_unique<RemoteTerminalHost>(
            RemoteTerminalHostOptions{
                .runtime_directory = temp.path,
                .client_id = "restart-client",
                .server_epoch = "restart-first",
                .method_prefix = "terminal",
                .terminal_id = std::string(
                    kServerShellTerminalId),
                .recovery = recovery,
                .presentation_suspend_supported = true,
            });

    {
        ServerKernel first_server({
            .runtime_directory = temp.path,
            .epoch_override = "restart-first",
        });
        REQUIRE(first_server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard first_run(first_server);
        REQUIRE(first_host->initialize(
            first_context, first_callbacks));
        REQUIRE(pump_until(*first_host, [&] {
            return first_host->status_text().find(
                       "remote controller")
                != std::string::npos;
        }));
        first_host->set_presentation_visible(false);
        bool suspended = false;
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            const auto metrics = ControlClient::request(
                namespaced_control_id(kServerControlId, temp.path),
                temp.path, "terminal.metrics");
            REQUIRE(metrics.ok);
            if (metrics.result["suspended_subscribers"] == 1)
            {
                suspended = true;
                break;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        REQUIRE(suspended);
        first_server.request_stop();
    }

    // The old wall-clock grace killed a pane after roughly one failed
    // request. Keep this pane disconnected longer than ten seconds to prove
    // recovery is attempt-based and remains alive during a real outage.
    const auto stalled_until
        = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(10250);
    while (std::chrono::steady_clock::now()
        < stalled_until)
    {
        first_host->pump();
        REQUIRE(first_host->is_running());
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    {
        ServerKernel second_server({
            .runtime_directory = temp.path,
            .epoch_override = "restart-second",
        });
        REQUIRE(second_server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard second_run(second_server);
        first_host->set_presentation_visible(true);
        REQUIRE(pump_until(*first_host, [&] {
            return recovery->server_epoch()
                == "restart-second"
                && first_host->status_text().find(
                       "remote controller")
                != std::string::npos;
        }));

        FakeTermRenderer second_renderer;
        TestHostCallbacks second_callbacks;
        HostContext second_context = first_context;
        second_context.grid_renderer = &second_renderer;
        RemoteTerminalHost projected_host({
            .runtime_directory = temp.path,
            .client_id = "new-projection-client",
            // Deliberately stale: the shared mutable recovery state is now
            // authoritative for hosts projected after the restart.
            .server_epoch = "restart-first",
            .method_prefix = "terminal",
            .terminal_id = std::string(
                kServerShellTerminalId),
            .recovery = recovery,
        });
        REQUIRE(projected_host.initialize(
            second_context, second_callbacks));
        REQUIRE(pump_until(projected_host, [&] {
            return projected_host.status_text().find(
                       "remote observer")
                != std::string::npos;
        }));
        CHECK(first_host->is_running());
        CHECK(projected_host.is_running());

        projected_host.shutdown();
        first_host->shutdown();
    }
}

TEST_CASE("remote terminal host renders shared state and can take control",
    "[host][remote-terminal]")
{
    TempDir temp("draxul-remote-host");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "host-test-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "render-client",
        .server_epoch = "host-test-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.grid_cols() == 20 && host.grid_rows() == 5;
    }));
    REQUIRE(renderer.last_handle != nullptr);
    REQUIRE(renderer.last_handle->total_cell_updates() > 0);
    REQUIRE(callbacks.last_window_title == "Draxul Fake Remote");
    REQUIRE(host.status_text().find("controller") != std::string::npos);

    RemoteTerminalClient observer({
        .runtime_directory = temp.path,
        .client_id = "observer",
        .expected_server_epoch = "host-test-epoch",
    });
    std::string error;
    REQUIRE(observer.attach(error));
    const bool observer_took_control = observer.take_control(error);
    INFO(error);
    INFO(observer.last_error_code());
    REQUIRE(observer_took_control);
    bool changed = false;
    const bool observer_polled = observer.poll(changed, error);
    INFO(error);
    INFO(observer.last_error_code());
    REQUIRE(observer_polled);
    const bool host_became_observer = pump_until(host, [&] {
        return host.status_text().find("observer") != std::string::npos;
    });
    INFO(host.status_text());
    INFO(host.is_running());
    INFO(callbacks.last_toast_message);
    REQUIRE(host_became_observer);

    const int observer_toasts_before = callbacks.push_toast_calls;
    window.clipboard_ = "observer-paste";
    host.on_key({
        .keycode = SDLK_A,
        .mod = kModNone,
        .pressed = true,
    });
    host.on_text_input({ .text = "a" });
    REQUIRE(host.dispatch_action("paste"));
    CHECK(callbacks.push_toast_calls == observer_toasts_before + 1);
    CHECK(callbacks.last_toast_message.find("Take Terminal Control")
        != std::string::npos);

    REQUIRE(host.dispatch_action("take_terminal_control"));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller") != std::string::npos;
    }));

    const size_t updates_before
        = renderer.last_handle->total_cell_updates();
    host.on_text_input({ .text = "shared-render" });
    REQUIRE(pump_until(host, [&] {
        return renderer.last_handle->total_cell_updates()
            > updates_before;
    }));
    const size_t incremental_updates
        = renderer.last_handle->total_cell_updates()
        - updates_before;
    CHECK(incremental_updates
        < static_cast<size_t>(host.grid_cols() * host.grid_rows()));
    REQUIRE(observer.poll(changed, error));
    REQUIRE(changed);

    host.shutdown();
}

TEST_CASE("hidden remote terminal host suspends presentation and resumes with current state",
    "[host][remote-terminal][suspend]")
{
    TempDir temp("draxul-remote-host-suspend");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "host-suspend-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    RecordingHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "hidden-render-client",
        .server_epoch = "host-suspend-epoch",
        .method_prefix = "terminal",
        .terminal_id = std::string(kServerShellTerminalId),
        .presentation_suspend_supported = true,
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos;
    }));
    REQUIRE(renderer.last_handle != nullptr);

    host.set_presentation_visible(false);
    bool suspended = false;
    nlohmann::json metrics;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        const auto response = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(response.ok);
        metrics = response.result;
        if (metrics["active_subscribers"] == 0
            && metrics["suspended_subscribers"] == 1)
        {
            suspended = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(suspended);

    host.pump();
    const int hidden_frame_requests = callbacks.request_frame_calls.load();
    const int hidden_window_wakes = callbacks.wake_window_calls.load();
    const size_t hidden_cell_updates
        = renderer.last_handle->total_cell_updates();

    RemoteTerminalClient writer({
        .runtime_directory = temp.path,
        .client_id = "hidden-render-client",
        .expected_server_epoch = "host-suspend-epoch",
        .method_prefix = "terminal",
        .terminal_id = std::string(kServerShellTerminalId),
    });
    std::string error;
#ifdef _WIN32
    const std::string command
        = "Write-Output '__HIDDEN_HOST_READY__'\r";
#else
    const std::string command
        = "printf '__HIDDEN_HOST_READY__\\n'\r";
#endif
    REQUIRE(writer.send_input(command, error, 1));

    bool avoided_delta = false;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        const auto response = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(response.ok);
        metrics = response.result;
        if (metrics["avoided_delta_encodes"].get<uint64_t>() > 0)
        {
            avoided_delta = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(avoided_delta);
    host.pump();
    CHECK(callbacks.request_frame_calls.load() == hidden_frame_requests);
    CHECK(callbacks.wake_window_calls.load() == hidden_window_wakes);
    CHECK(renderer.last_handle->total_cell_updates()
        == hidden_cell_updates);
    CHECK_FALSE(host.next_deadline().has_value());

#ifdef _WIN32
    host.on_text_input({
        .text = "1..20 | ForEach-Object { Write-Output (\"__HIDDEN_{0:D2}__\" -f $_) }\r",
    });
#else
    host.on_text_input({
        .text = "for i in $(seq 1 20); do printf '__HIDDEN_%02d__\\n' \"$i\"; done\r",
    });
#endif
    bool command_completed_while_hidden = false;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        const auto response = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(response.ok);
        if (response.result["resumes"].get<uint64_t>() >= 1
            && response.result["suspensions"].get<uint64_t>() >= 2
            && response.result["suspended_subscribers"] == 1)
        {
            command_completed_while_hidden = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(command_completed_while_hidden);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    auto hidden_viewport = context.initial_viewport;
    hidden_viewport.pixel_size = { 480, 224 };
    hidden_viewport.grid_size = { 30, 7 };
    host.set_viewport(hidden_viewport);
    bool resized_while_hidden = false;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        const auto response = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        REQUIRE(response.ok);
        if (response.result["resumes"].get<uint64_t>() >= 2
            && response.result["suspensions"].get<uint64_t>() >= 3
            && response.result["suspended_subscribers"] == 1)
        {
            resized_while_hidden = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(resized_while_hidden);

    host.set_presentation_visible(true);
    REQUIRE(pump_until(host, [&] {
        const auto response = ControlClient::request(
            namespaced_control_id(kServerControlId, temp.path), temp.path,
            "terminal.metrics");
        return response.ok
            && response.result["active_subscribers"] == 1
            && response.result["suspended_subscribers"] == 0
            && host.grid_cols() == 30
            && host.grid_rows() == 7
            && renderer.last_handle->total_cell_updates()
            >= hidden_cell_updates
                + static_cast<size_t>(
                    host.grid_cols() * host.grid_rows());
    }));
    CHECK(host.next_deadline().has_value());
    CHECK(callbacks.request_frame_calls.load() > hidden_frame_requests);
    CHECK(callbacks.wake_window_calls.load() > hidden_window_wakes);

    RemoteTerminalClient observer({
        .runtime_directory = temp.path,
        .client_id = "hidden-state-observer",
        .expected_server_epoch = "host-suspend-epoch",
        .method_prefix = "terminal",
        .terminal_id = std::string(kServerShellTerminalId),
    });
    REQUIRE(observer.attach(error));
    CHECK(observer.projection().snapshot().cols == host.grid_cols());
    CHECK(observer.projection().snapshot().rows == host.grid_rows());
    CHECK(observer.projection().version().sequence > 0);
    RemoteTerminalScrollbackPage scrollback;
    bool scrollback_ready = false;
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        REQUIRE(observer.read_scrollback(
            1, 5, scrollback, error));
        if (scrollback.total_rows > 0
            && scrollback.snapshot.has_value())
        {
            scrollback_ready = true;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(scrollback_ready);
    CHECK(scrollback.snapshot->cols == host.grid_cols());

    host.shutdown();
}

TEST_CASE("remote terminal host preserves updates published before the UI pumps",
    "[host][remote-terminal][render][recovery]")
{
    TempDir temp("draxul-remote-host-published-state");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "published-state-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    RecordingHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "published-state-client",
        .server_epoch = "published-state-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos;
    }));
    REQUIRE(renderer.last_handle != nullptr);
    renderer.last_handle->reset();

    const auto wait_for_wake_after = [&](int previous) {
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            if (callbacks.wake_window_calls.load() > previous)
                return true;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        return false;
    };

    int wake_count = callbacks.wake_window_calls.load();
    host.on_text_input({ .text = "A" });
    REQUIRE(wait_for_wake_after(wake_count));
    wake_count = callbacks.wake_window_calls.load();
    host.on_text_input({ .text = "B" });
    REQUIRE(wait_for_wake_after(wake_count));

    host.pump();
    CHECK(renderer.last_handle->total_cell_updates()
        >= static_cast<size_t>(
            host.grid_cols() * host.grid_rows()));
    host.shutdown();
}

TEST_CASE("remote terminal host attaches to its projected terminal identity",
    "[host][remote-terminal][topology]")
{
    TempDir temp("draxul-remote-host-topology");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "host-topology-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    TopologyClient topology({
        .runtime_directory = temp.path,
        .client_id = "topology-host-client",
    });
    std::string error;
    REQUIRE(topology.refresh(error));
    const TopologySpace& initial_space
        = topology.snapshot().spaces.front();
    const TopologyTab& initial_tab
        = initial_space.tabs.front();
    TopologyCommandResult split;
    REQUIRE(topology.execute({
                                 .command_id = "host-dynamic-terminal",
                                 .expected_revision = topology.snapshot().revision,
                                 .kind = TopologyCommandKind::SplitPane,
                                 .space_id = initial_space.space_id,
                                 .tab_id = initial_tab.tab_id,
                                 .pane_id = initial_tab.panes.front().pane_id,
                                 .name = "Projected terminal",
                                 .direction = TopologySplitDirection::Vertical,
                                 .pane_domain = TopologyPaneDomain::ServerTerminal,
                             },
        split, error));
    const TopologyPane& projected
        = split.snapshot.spaces.front().tabs.front().panes.back();
    REQUIRE_FALSE(projected.terminal_id.empty());

    RemoteTerminalClient controller({
        .runtime_directory = temp.path,
        .client_id = "dynamic-controller",
        .expected_server_epoch = "host-topology-epoch",
        .method_prefix = "terminal",
        .terminal_id = projected.terminal_id,
    });
    REQUIRE(controller.attach(error));
    REQUIRE(controller.resize(13, 4, error));

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    TestHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "dynamic-observer",
        .server_epoch = "host-topology-epoch",
        .method_prefix = "terminal",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
            .remote_terminal_id = projected.terminal_id,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.grid_cols() == 13
            && host.grid_rows() == 4;
    }));

    TopologyCommandResult closed;
    REQUIRE(topology.execute({
                                 .command_id = "close-host-dynamic-terminal",
                                 .expected_revision = split.snapshot.revision,
                                 .kind = TopologyCommandKind::ClosePane,
                                 .space_id = initial_space.space_id,
                                 .tab_id = initial_tab.tab_id,
                                 .pane_id = projected.pane_id,
                             },
        closed, error));
    REQUIRE(pump_until(host, [&] {
        return !host.is_running();
    }));
    CHECK(callbacks.last_toast_message.empty());
    host.shutdown();

    RemoteTerminalHost missing({
        .runtime_directory = temp.path,
        .client_id = "stale-topology-client",
        .server_epoch = "host-topology-epoch",
        .method_prefix = "terminal",
    });
    HostContext missing_context = context;
    missing_context.launch_options.remote_terminal_id
        = projected.terminal_id;
    REQUIRE(missing.initialize(missing_context, callbacks));
    REQUIRE(pump_until(missing, [&] {
        return !missing.is_running();
    }));
    CHECK(missing.init_error_code() == "terminal_not_found");
}

TEST_CASE("two rendered remote terminal hosts survive repeated control transfer",
    "[host][remote-terminal][takeover]")
{
    TempDir temp("draxul-remote-host-takeover");
    ServerKernel server({
        .runtime_directory = temp.path,
        // This test exercises transport recovery and repeated control changes,
        // not lease expiry. Leave enough headroom for a Windows debug build.
        .client_activity_timeout = std::chrono::seconds(2),
        .epoch_override = "takeover-test-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    TextService text_service;
    draxul::tests::init_text_service(text_service);

    FakeWindow first_window;
    FakeWindow second_window;
    FakeTermRenderer first_renderer;
    FakeTermRenderer second_renderer;
    TestHostCallbacks first_callbacks;
    TestHostCallbacks second_callbacks;
    RemoteTerminalHost first({
        .runtime_directory = temp.path,
        .client_id = "first-render-client",
        .server_epoch = "takeover-test-epoch",
    });
    RemoteTerminalHost second({
        .runtime_directory = temp.path,
        .client_id = "second-render-client",
        .server_epoch = "takeover-test-epoch",
    });
    HostContext first_context{
        .window = &first_window,
        .grid_renderer = &first_renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 20, 5 },
        },
        .display_ppi = 96.0f,
    };
    HostContext second_context = first_context;
    second_context.window = &second_window;
    second_context.grid_renderer = &second_renderer;
    second_context.initial_viewport.pixel_size = { 480, 192 };
    second_context.initial_viewport.grid_size = { 30, 6 };

    REQUIRE(first.initialize(first_context, first_callbacks));
    REQUIRE(pump_until(first, [&] {
        return first.status_text().find("controller")
            != std::string::npos;
    }));
    REQUIRE(second.initialize(second_context, second_callbacks));
    REQUIRE(pump_until(first, [&] {
        second.pump();
        return first.status_text().find("controller") != std::string::npos
            && second.status_text().find("observer") != std::string::npos;
    }));

    const auto metadata_path = server_metadata_path(temp.path);
    auto held_metadata_path = metadata_path;
    held_metadata_path += ".held";
    REQUIRE(rename_with_retry(metadata_path, held_metadata_path));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const bool survived_transport_gap
        = first.is_running() && second.is_running();
    REQUIRE(rename_with_retry(held_metadata_path, metadata_path));
    REQUIRE(survived_transport_gap);
    pump_for(first, second, std::chrono::milliseconds(500));
    REQUIRE(first.is_running());
    REQUIRE(second.is_running());

    const auto input_deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < input_deadline)
    {
        first.on_text_input({
            .text = "abcdefghijklmnopqrstuvwxyz0123456789",
        });
        first.pump();
        second.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    pump_for(first, second, std::chrono::seconds(6));
    REQUIRE(first.is_running());
    REQUIRE(second.is_running());

    for (int transfer = 0; transfer < 20; ++transfer)
    {
        auto& next = transfer % 2 == 0 ? second : first;
        auto& previous = transfer % 2 == 0 ? first : second;
        REQUIRE(next.dispatch_action("take_terminal_control"));
        REQUIRE(pump_until(next, [&] {
            previous.pump();
            return next.status_text().find("controller") != std::string::npos
                && previous.status_text().find("observer") != std::string::npos;
        }));
        const int expected_cols = transfer % 2 == 0 ? 30 : 20;
        const int expected_rows = transfer % 2 == 0 ? 6 : 5;
        INFO("transfer=" << transfer
                         << " first=" << first.grid_cols() << "x" << first.grid_rows()
                         << " second=" << second.grid_cols() << "x" << second.grid_rows());
        REQUIRE(pump_until(next, [&] {
            previous.pump();
            return next.grid_cols() == expected_cols
                && next.grid_rows() == expected_rows
                && previous.grid_cols() == expected_cols
                && previous.grid_rows() == expected_rows;
        }));
        REQUIRE(first.is_running());
        REQUIRE(second.is_running());
    }

    first.shutdown();
    second.shutdown();
}

TEST_CASE("remote terminal local navigation copy mode and titles stay client-local",
    "[host][remote-terminal][copy-mode][scrollback][title]")
{
    TempDir temp("draxul-remote-host-local-navigation");
    ControlServer control;
    std::string control_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &control_error));

    const auto live_snapshot = snapshot_from_rows(
        { "LIVE0001", "LIVE0002", "LIVE0003" },
        "Remote Title One");
    const RemoteTerminalVersion initial_version{
        .server_epoch = "local-navigation-epoch",
        .terminal_id = std::string(kFakeRemoteTerminalId),
        .generation = 1,
    };
    const RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Local Navigation",
            .execution_domain = "server_terminal",
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = initial_version,
            .controller_client_id = "local-navigation-client",
            .snapshot = live_snapshot,
        },
    };
    std::atomic<bool> emit_same_title_event = false;
    std::atomic<bool> same_title_event_sent = false;
    std::atomic<bool> emit_new_title_event = false;
    std::atomic<bool> new_title_event_sent = false;
    std::atomic<bool> emit_empty_title_event = false;
    std::atomic<bool> empty_title_event_sent = false;
    std::atomic<int> input_calls = 0;
    std::atomic<int> scrollback_calls = 0;
    std::jthread dispatcher([&](std::stop_token stop) {
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
                    nlohmann::json events = nlohmann::json::array();
                    if (emit_same_title_event
                        && !same_title_event_sent.exchange(true))
                    {
                        auto version = initial_version;
                        version.sequence = 1;
                        events.push_back(remote_terminal_event_to_json({
                            .kind = RemoteTerminalEventKind::Controller,
                            .version = version,
                            .controller_client_id
                            = "local-navigation-client",
                        }));
                    }
                    else if (emit_new_title_event
                        && !new_title_event_sent.exchange(true))
                    {
                        auto changed_snapshot = live_snapshot;
                        changed_snapshot.metadata.title
                            = "Remote Title Two";
                        auto version = initial_version;
                        version.sequence = 2;
                        events.push_back(remote_terminal_event_to_json({
                            .kind = RemoteTerminalEventKind::Snapshot,
                            .version = version,
                            .controller_client_id
                            = "local-navigation-client",
                            .snapshot = std::move(changed_snapshot),
                        }));
                    }
                    else if (emit_empty_title_event
                        && !empty_title_event_sent.exchange(true))
                    {
                        auto changed_snapshot = live_snapshot;
                        changed_snapshot.metadata.title.clear();
                        auto version = initial_version;
                        version.sequence = 3;
                        events.push_back(remote_terminal_event_to_json({
                            .kind = RemoteTerminalEventKind::Snapshot,
                            .version = version,
                            .controller_client_id
                            = "local-navigation-client",
                            .snapshot = std::move(changed_snapshot),
                        }));
                    }
                    return ControlMethodResult::success({
                        { "events", std::move(events) },
                    });
                }
                if (request.method == "fake.scrollback")
                {
                    const int call = ++scrollback_calls;
                    const uint64_t offset = std::min<uint64_t>(
                        request.params.value(
                            "offset_from_live", uint64_t{ 0 }),
                        12);
                    auto version = initial_version;
                    version.sequence = empty_title_event_sent
                        ? 3
                        : (new_title_event_sent ? 2 : 0);
                    if (call == 1)
                    {
                        return ControlMethodResult::success(
                            remote_terminal_scrollback_page_to_json({
                                .version = version,
                                .total_rows = 12,
                                .offset_from_live = offset,
                                .cols = 8,
                                .snapshot = snapshot_from_rows(
                                    { "HISTORY1", "HISTORY2", "HISTORY3" }),
                            }));
                    }
                    return ControlMethodResult::success(
                        remote_terminal_scrollback_page_to_json({
                            .version = version,
                            .total_rows = 12,
                            .offset_from_live = offset,
                            .cols = 7,
                            .snapshot = snapshot_from_rows(
                                { "OLD0001", "OLD0002", "OLD0003" }),
                        }));
                }
                if (request.method == "fake.input")
                {
                    ++input_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::success(
                    nlohmann::json::object());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);
    RecordingHostCallbacks callbacks;
    RemoteTerminalHost host({
        .runtime_directory = temp.path,
        .client_id = "local-navigation-client",
        .server_epoch = "local-navigation-epoch",
    });
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
            .copy_on_select = false,
        },
        .initial_viewport = {
            .pixel_size = { 320, 160 },
            .grid_size = { 8, 3 },
        },
        .display_ppi = 96.0f,
    };
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("controller")
            != std::string::npos
            && callbacks.window_title_calls.load() == 1;
    }));
    CHECK(callbacks.last_window_title == "Remote Title One");

    emit_same_title_event = true;
    REQUIRE(pump_until(host, [&] {
        return same_title_event_sent.load()
            && callbacks.wake_window_calls.load() >= 2;
    }));
    CHECK(callbacks.window_title_calls.load() == 1);

    emit_new_title_event = true;
    REQUIRE(pump_until(host, [&] {
        return new_title_event_sent.load()
            && callbacks.window_title_calls.load() == 2;
    }));
    CHECK(callbacks.last_window_title == "Remote Title Two");

    emit_empty_title_event = true;
    REQUIRE(pump_until(host, [&] {
        return empty_title_event_sent.load()
            && callbacks.window_title_calls.load() == 3;
    }));
    CHECK(callbacks.last_window_title == "Draxul");

    host.on_mouse_button({
        .button = 1,
        .pressed = true,
        .mod = kModNone,
        .pos = { 1, 1 },
        .clicks = 2,
    });
    host.on_key({
        .keycode = SDLK_C,
        .mod = kModCtrl,
        .pressed = true,
    });
    host.on_text_input({ .text = "c" });
    CHECK(window.clipboard_ == "LIVE0001");
    CHECK(input_calls.load() == 0);

    host.on_key({
        .keycode = SDLK_PAGEUP,
        .mod = kModShift,
        .pressed = true,
    });
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("[3/12]")
            != std::string::npos;
    }));
    CHECK(input_calls.load() == 0);

    REQUIRE(host.dispatch_action("toggle_copy_mode"));
    host.on_key({ .keycode = SDLK_V, .pressed = true });
    host.on_key({ .keycode = SDLK_RIGHT, .pressed = true });
    host.on_key({ .keycode = SDLK_RIGHT, .pressed = true });
    host.on_key({ .keycode = SDLK_Y, .pressed = true });
    CHECK(window.clipboard_ == "HIS");
    CHECK(input_calls.load() == 0);

    host.on_key({
        .keycode = SDLK_END,
        .mod = kModShift,
        .pressed = true,
    });
    REQUIRE(pump_until(host, [&] {
        return host.status_text().find("[")
            == std::string::npos;
    }));
    REQUIRE(host.dispatch_action("toggle_copy_mode"));
    host.on_key({ .keycode = SDLK_V, .pressed = true });
    host.on_key({ .keycode = SDLK_RIGHT, .pressed = true });
    host.on_key({ .keycode = SDLK_RIGHT, .pressed = true });
    host.on_key({ .keycode = SDLK_Y, .pressed = true });
    CHECK(window.clipboard_ == "LIV");

    // A scrollback page captured at the previous terminal width is stale.
    // The host must return to live immediately and clear its status offset.
    host.on_key({
        .keycode = SDLK_PAGEUP,
        .mod = kModShift,
        .pressed = true,
    });
    REQUIRE(pump_until(host, [&] {
        return scrollback_calls.load() >= 2;
    }));
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        host.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(host.status_text().find("[") == std::string::npos);
    CHECK(callbacks.window_title_calls.load() == 3);
    CHECK(input_calls.load() == 0);
    CHECK(host.is_running());

    host.shutdown();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote terminal hosts scroll and select server history independently",
    "[host][remote-terminal][scrollback][selection]")
{
    TempDir temp("draxul-remote-host-scrollback");
    ServerKernel server({
        .runtime_directory = temp.path,
        .epoch_override = "host-scrollback-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    TextService text_service;
    draxul::tests::init_text_service(text_service);

    FakeWindow first_window;
    FakeWindow second_window;
    FakeTermRenderer first_renderer;
    FakeTermRenderer second_renderer;
    TestHostCallbacks first_callbacks;
    TestHostCallbacks second_callbacks;
    RemoteTerminalHost first({
        .runtime_directory = temp.path,
        .client_id = "scroll-host-a",
        .server_epoch = "host-scrollback-epoch",
        .method_prefix = "terminal",
    });
    RemoteTerminalHost second({
        .runtime_directory = temp.path,
        .client_id = "scroll-host-b",
        .server_epoch = "host-scrollback-epoch",
        .method_prefix = "terminal",
    });
    HostContext first_context{
        .window = &first_window,
        .grid_renderer = &first_renderer,
        .text_service = &text_service,
        .launch_options = {
            .kind = HostKind::RemoteTerminal,
            .copy_on_select = false,
        },
        .initial_viewport = {
            .pixel_size = { 320, 128 },
            .grid_size = { 40, 8 },
        },
        .display_ppi = 96.0f,
    };
    HostContext second_context = first_context;
    second_context.window = &second_window;
    second_context.grid_renderer = &second_renderer;

    REQUIRE(first.initialize(first_context, first_callbacks));
    REQUIRE(pump_until(first, [&] {
        return first.status_text().find("controller")
            != std::string::npos;
    }));
    REQUIRE(second.initialize(second_context, second_callbacks));
    REQUIRE(pump_until(first, [&] {
        second.pump();
        return first.status_text().find("controller") != std::string::npos
            && second.status_text().find("observer") != std::string::npos;
    }));

    RemoteTerminalClient monitor({
        .runtime_directory = temp.path,
        .client_id = "scroll-monitor",
        .expected_server_epoch = "host-scrollback-epoch",
        .method_prefix = "terminal",
    });
    std::string error;
    REQUIRE(monitor.attach(error));
#ifdef _WIN32
    const std::string command
        = "1..40 | % { Write-Output (\"__VIEW_{0:D2}__\" -f $_) }\r";
#else
    const std::string command
        = "for i in $(seq 1 40); do printf '__VIEW_%02d__\\n' \"$i\"; done\r";
#endif
    first.on_text_input({ .text = command });
    REQUIRE(wait_for_scrollback(monitor, 8, error));
    INFO(error);
    pump_for(first, second, std::chrono::milliseconds(250));

    first.on_mouse_wheel({
        .delta = { 0.0f, 3.0f },
        .mod = kModNone,
        .pos = { 8, 16 },
    });
    REQUIRE(pump_until(first, [&] {
        second.pump();
        return first.status_text().find("[") != std::string::npos;
    }));
    REQUIRE(second.status_text().find("[") == std::string::npos);
    REQUIRE(first_renderer.last_handle != nullptr);
    REQUIRE(first_renderer.last_handle->last_cursor.y == -1);

    first.on_mouse_button({
        .button = 1,
        .pressed = true,
        .mod = kModNone,
        .pos = { 8, 16 },
        .clicks = 2,
    });
    REQUIRE(first.dispatch_action("copy"));
    REQUIRE_FALSE(first_window.clipboard_.empty());

    const size_t updates_before_return_to_live
        = first_renderer.last_handle->total_cell_updates();
    first.on_text_input({ .text = "\r" });
    REQUIRE(pump_until(first, [&] {
        second.pump();
        return first.status_text().find("[") == std::string::npos;
    }));
    REQUIRE(second.status_text().find("[") == std::string::npos);
    CHECK(first_renderer.last_handle->total_cell_updates()
            - updates_before_return_to_live
        >= static_cast<size_t>(
            first.grid_cols() * first.grid_rows()));

    first.shutdown();
    second.shutdown();
}

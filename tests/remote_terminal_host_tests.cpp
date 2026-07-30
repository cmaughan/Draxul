#include <catch2/catch_test_macros.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <draxul/remote_terminal_client.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/text_service.h>
#include <draxul/topology_client.h>

#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT)
        + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

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
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        host.pump();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
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

} // namespace

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
    TextServiceConfig text_config;
    text_config.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(
        text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));
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
    TextServiceConfig text_config;
    text_config.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(
        text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));

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
    REQUIRE(observer.poll(changed, error));
    REQUIRE(changed);

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
    TextServiceConfig text_config;
    text_config.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(
        text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));
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
    CHECK_FALSE(missing.initialize(
        missing_context, callbacks));
    CHECK(missing.init_error_code() == "terminal_not_found");
}

TEST_CASE("two rendered remote terminal hosts survive repeated control transfer",
    "[host][remote-terminal][takeover]")
{
    TempDir temp("draxul-remote-host-takeover");
    ServerKernel server({
        .runtime_directory = temp.path,
        .client_activity_timeout
        = std::chrono::milliseconds(150),
        .epoch_override = "takeover-test-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard server_run(server);

    TextService text_service;
    TextServiceConfig text_config;
    text_config.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(
        text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));

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
    TextServiceConfig text_config;
    text_config.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(
        text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));

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

    first.on_text_input({ .text = "\r" });
    REQUIRE(pump_until(first, [&] {
        second.pump();
        return first.status_text().find("[") == std::string::npos;
    }));
    REQUIRE(second.status_text().find("[") == std::string::npos);

    first.shutdown();
    second.shutdown();
}

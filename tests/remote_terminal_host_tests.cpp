#include <catch2/catch_test_macros.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <draxul/remote_terminal_client.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/server_kernel.h>
#include <draxul/text_service.h>

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

} // namespace

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

#include "support/pty_capture_fixture.h"
#include "support/scoped_env_var.h"
#include "support/test_local_terminal_host.h"
#include "support/test_terminal_host_fixture.h"
#include "support/test_vt_terminal_host.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("terminal raw PTY capture records drained chunks for replay", "[terminal][capture]")
{
    const auto capture_path
        = std::filesystem::temp_directory_path() / "draxul-terminal-capture-test.log";
    std::filesystem::remove(capture_path);

    const draxul::tests::ScopedEnvVar capture_env("DRAXUL_CAPTURE_PTY_FILE", capture_path.string().c_str());
    draxul::tests::TerminalHostFixture<draxul::tests::TestVtTerminalHost> fixture;
    REQUIRE(fixture.ok);

    fixture.host.queue_drain("hello ");
    fixture.host.queue_drain("\x1b[31mworld\x1b[0m");
    fixture.host.pump();

    auto capture = draxul::tests::load_pty_capture_file(capture_path);
    REQUIRE(capture.has_value());
    REQUIRE(capture->chunks.size() == 2);
    CHECK(capture->chunks[0].host_name == "test");
    CHECK(capture->chunks[0].bytes == "hello ");
    CHECK(capture->chunks[1].host_name == "test");
    CHECK(capture->chunks[1].bytes == "\x1b[31mworld\x1b[0m");

    std::filesystem::remove(capture_path);
}

TEST_CASE("terminal raw PTY capture replay reproduces the final terminal state", "[terminal][capture]")
{
    const auto capture_path
        = std::filesystem::temp_directory_path() / "draxul-terminal-capture-replay-test.log";
    std::filesystem::remove(capture_path);

    {
        const draxul::tests::ScopedEnvVar capture_env("DRAXUL_CAPTURE_PTY_FILE", capture_path.string().c_str());
        draxul::tests::TerminalHostFixture<draxul::tests::TestVtTerminalHost> fixture;
        REQUIRE(fixture.ok);

        fixture.host.queue_drain("abc");
        fixture.host.queue_drain("\r\n");
        fixture.host.queue_drain("\x1b[32mxyz\x1b[0m");
        fixture.host.pump();
    }

    auto capture = draxul::tests::load_pty_capture_file(capture_path);
    REQUIRE(capture.has_value());

    draxul::tests::TestVtTerminalHost replay;
    draxul::HostViewport vp;
    vp.grid_size = { 20, 5 };
    draxul::tests::FakeWindow window;
    draxul::tests::FakeTermRenderer renderer;
    draxul::TextService text_service;
    draxul::TextServiceConfig ts_cfg;
    ts_cfg.font_path = draxul::tests::bundled_font_path().string();
    text_service.initialize(ts_cfg, draxul::TextService::DEFAULT_POINT_SIZE, 96.0f);
    draxul::tests::TestHostCallbacks callbacks;
    draxul::HostContext ctx{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = vp,
    };
    REQUIRE(replay.initialize(ctx, callbacks));

    for (const auto& chunk : capture->chunks)
        replay.feed(chunk.bytes);

    CHECK(replay.cell_text(0, 0) == "a");
    CHECK(replay.cell_text(1, 0) == "b");
    CHECK(replay.cell_text(2, 0) == "c");
    CHECK(replay.cell_text(0, 1) == "x");
    CHECK(replay.cell_text(1, 1) == "y");
    CHECK(replay.cell_text(2, 1) == "z");
    CHECK(replay.row() == 1);
    CHECK(replay.col() == 3);

    std::filesystem::remove(capture_path);
}

TEST_CASE("terminal raw PTY capture honors the host launch capture path", "[terminal][capture]")
{
    const auto capture_path
        = std::filesystem::temp_directory_path() / "draxul-terminal-launch-capture-test.log";
    std::filesystem::remove(capture_path);

    draxul::tests::TestVtTerminalHost host;
    draxul::HostViewport vp;
    vp.grid_size = { 20, 5 };
    draxul::tests::FakeWindow window;
    draxul::tests::FakeTermRenderer renderer;
    draxul::TextService text_service;
    draxul::TextServiceConfig ts_cfg;
    ts_cfg.font_path = draxul::tests::bundled_font_path().string();
    text_service.initialize(ts_cfg, draxul::TextService::DEFAULT_POINT_SIZE, 96.0f);
    draxul::tests::TestHostCallbacks callbacks;
    draxul::HostContext ctx{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = draxul::HostLaunchOptions{ .pty_capture_file = capture_path.string() },
        .initial_viewport = vp,
    };
    REQUIRE(host.initialize(ctx, callbacks));

    host.queue_drain("capture me");
    host.pump();

    auto capture = draxul::tests::load_pty_capture_file(capture_path);
    REQUIRE(capture.has_value());
    REQUIRE(capture->chunks.size() == 1);
    CHECK(capture->chunks[0].bytes == "capture me");

    std::filesystem::remove(capture_path);
}

TEST_CASE("terminal raw PTY capture works for local terminal hosts", "[terminal][capture]")
{
    const auto capture_path
        = std::filesystem::temp_directory_path() / "draxul-local-terminal-capture-test.log";
    std::filesystem::remove(capture_path);

    draxul::tests::FakeWindow window;
    draxul::tests::FakeTermRenderer renderer;
    draxul::TextService text_service;
    draxul::TextServiceConfig ts_cfg;
    ts_cfg.font_path = draxul::tests::bundled_font_path().string();
    text_service.initialize(ts_cfg, draxul::TextService::DEFAULT_POINT_SIZE, 96.0f);
    draxul::tests::TestHostCallbacks callbacks;
    draxul::tests::TestLocalTerminalHost host;
    draxul::HostViewport vp;
    vp.grid_size = { 20, 5 };
    draxul::HostContext ctx{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .launch_options = draxul::HostLaunchOptions{ .pty_capture_file = capture_path.string() },
        .initial_viewport = vp,
    };
    REQUIRE(host.initialize(ctx, callbacks));

    host.queue_drain("local capture");
    host.pump();

    auto capture = draxul::tests::load_pty_capture_file(capture_path);
    REQUIRE(capture.has_value());
    REQUIRE(capture->chunks.size() == 1);
    CHECK(capture->chunks[0].host_name == "test");
    CHECK(capture->chunks[0].bytes == "local capture");

    std::filesystem::remove(capture_path);
}

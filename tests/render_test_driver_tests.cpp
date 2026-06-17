#include <catch2/catch_test_macros.hpp>
#include <draxul/render_test_driver.h>

namespace draxul
{
namespace
{

class FakeCaptureRenderer : public ICaptureRenderer
{
public:
    void request_frame_capture() override
    {
        requested = true;
    }

    std::optional<CapturedFrame> take_captured_frame() override
    {
        if (!available)
            return std::nullopt;
        available = false;
        CapturedFrame frame;
        frame.width = 1;
        frame.height = 1;
        frame.rgba = { 0, 0, 0, 255 };
        return frame;
    }

    bool requested = false;
    bool available = false;
};

RenderTestDriverEnv make_env(FakeCaptureRenderer& capture)
{
    RenderTestDriverEnv env;
    env.capture = [&capture]() { return &capture; };
    return env;
}

} // namespace

TEST_CASE("render test driver captures after ready content settles")
{
    FakeCaptureRenderer capture;
    auto env = make_env(capture);
    bool running = true;
    bool saw_frame = true;
    bool frame_requested = false;
    HostRuntimeState state;
    state.content_ready = true;

    env.pump_once = [&](std::optional<std::chrono::steady_clock::time_point>) {
        if (capture.requested)
            capture.available = true;
        return true;
    };
    env.request_frame = [&]() {
        frame_requested = true;
        frame_requested = false;
    };
    env.is_running = [&]() { return running; };
    env.saw_frame = [&]() { return saw_frame; };
    env.frame_requested = [&]() { return frame_requested; };
    env.active_host_state = [&]() -> std::optional<HostRuntimeState> { return state; };

    const auto result = run_render_test_driver(env, { std::chrono::milliseconds(250), std::chrono::milliseconds(0) });

    REQUIRE(result.frame.has_value());
    CHECK(result.error.empty());
    CHECK(capture.requested);
}

TEST_CASE("render test driver waits for diagnostics panel before capture")
{
    FakeCaptureRenderer capture;
    auto env = make_env(capture);
    bool diagnostics_enabled = false;
    std::optional<std::chrono::steady_clock::time_point> diagnostics_time;
    HostRuntimeState state;
    state.content_ready = true;

    env.pump_once = [&](std::optional<std::chrono::steady_clock::time_point>) {
        if (diagnostics_enabled && !diagnostics_time)
            diagnostics_time = std::chrono::steady_clock::now();
        if (capture.requested)
            capture.available = true;
        return true;
    };
    env.request_frame = []() {};
    env.is_running = []() { return true; };
    env.saw_frame = []() { return true; };
    env.frame_requested = []() { return false; };
    env.active_host_state = [&]() -> std::optional<HostRuntimeState> { return state; };
    env.enable_diagnostics_panel = [&]() {
        diagnostics_enabled = true;
        diagnostics_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    };
    env.diagnostics_panel_render_time = [&]() { return diagnostics_time; };

    RenderTestDriverOptions options;
    options.timeout = std::chrono::milliseconds(250);
    options.settle = std::chrono::milliseconds(0);
    options.want_diagnostics = true;
    const auto result = run_render_test_driver(env, options);

    REQUIRE(result.frame.has_value());
    CHECK(result.error.empty());
    CHECK(diagnostics_enabled);
    CHECK(capture.requested);
}

TEST_CASE("render test driver reports host timeout")
{
    FakeCaptureRenderer capture;
    auto env = make_env(capture);
    env.pump_once = [](std::optional<std::chrono::steady_clock::time_point>) { return true; };
    env.request_frame = []() {};
    env.is_running = []() { return true; };
    env.saw_frame = []() { return false; };
    env.frame_requested = []() { return false; };
    env.active_host_state = []() -> std::optional<HostRuntimeState> { return std::nullopt; };

    const auto result = run_render_test_driver(env, { std::chrono::milliseconds(1), std::chrono::milliseconds(0) });

    CHECK_FALSE(result.frame.has_value());
    CHECK(result.error == "Timed out waiting for a stable render capture (no host)");
}

TEST_CASE("render test driver reports capture timeout phase")
{
    FakeCaptureRenderer capture;
    auto env = make_env(capture);
    HostRuntimeState state;
    state.content_ready = true;

    env.pump_once = [](std::optional<std::chrono::steady_clock::time_point>) { return true; };
    env.request_frame = []() {};
    env.is_running = []() { return true; };
    env.saw_frame = []() { return true; };
    env.frame_requested = []() { return false; };
    env.active_host_state = [&]() -> std::optional<HostRuntimeState> { return state; };

    const auto result = run_render_test_driver(env, { std::chrono::milliseconds(1), std::chrono::milliseconds(0) });

    CHECK_FALSE(result.frame.has_value());
    CHECK(result.error.find("phase=Capturing") != std::string::npos);
    CHECK(result.error.find("capture_requested=true") != std::string::npos);
}

} // namespace draxul

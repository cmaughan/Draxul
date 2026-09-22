#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <draxul/capture_renderer.h>
#include <draxul/host.h>

namespace draxul
{

struct RenderTestDriverEnv
{
    std::function<bool(std::optional<std::chrono::steady_clock::time_point>)> pump_once;
    std::function<void()> request_frame;
    std::function<bool()> is_running;
    std::function<bool()> saw_frame;
    std::function<uint64_t()> rendered_frame_count;
    std::function<bool()> frame_requested;
    std::function<std::optional<HostRuntimeState>()> active_host_state;
    std::function<ICaptureRenderer*()> capture;
    std::function<void()> enable_diagnostics_panel;
    std::function<std::optional<std::chrono::steady_clock::time_point>()> diagnostics_panel_render_time;
    // Runs once after the original content has rendered and settled. An empty
    // result means success; a message aborts the render test. Capture cannot
    // begin until a newer frame has rendered after this hook.
    std::function<std::string()> before_capture;
};

struct RenderTestDriverOptions
{
    std::chrono::milliseconds timeout{ 0 };
    std::chrono::milliseconds settle{ 0 };
    bool want_diagnostics = false;
};

struct RenderTestDriverResult
{
    std::optional<CapturedFrame> frame;
    std::string error;
};

RenderTestDriverResult run_render_test_driver(RenderTestDriverEnv& env, const RenderTestDriverOptions& options);

} // namespace draxul

#include <draxul/render_test_driver.h>

#include <algorithm>
#include <sstream>

namespace draxul
{
namespace
{

enum class RenderTestPhase
{
    kWaitingForContent,
    kSettlingContent,
    kEnablingDiagnostics,
    kSettlingForCapture,
    kCapturing,
};

const char* render_test_phase_name(RenderTestPhase p)
{
    switch (p)
    {
    case RenderTestPhase::kWaitingForContent:
        return "WaitingForContent";
    case RenderTestPhase::kSettlingContent:
        return "SettlingContent";
    case RenderTestPhase::kEnablingDiagnostics:
        return "EnablingDiagnostics";
    case RenderTestPhase::kSettlingForCapture:
        return "SettlingForCapture";
    case RenderTestPhase::kCapturing:
        return "Capturing";
    }
    return "Unknown";
}

struct RenderTestContext
{
    RenderTestPhase phase = RenderTestPhase::kWaitingForContent;
    std::chrono::steady_clock::time_point settle_start{};
    bool quiet_observed = false;
    std::optional<std::chrono::steady_clock::time_point> diagnostics_enabled_at;
};

ICaptureRenderer* capture_renderer(RenderTestDriverEnv& env)
{
    if (!env.capture)
        return nullptr;
    return env.capture();
}

bool env_is_running(RenderTestDriverEnv& env)
{
    return !env.is_running || env.is_running();
}

std::optional<HostRuntimeState> active_host_state(RenderTestDriverEnv& env)
{
    if (!env.active_host_state)
        return std::nullopt;
    return env.active_host_state();
}

bool env_saw_frame(RenderTestDriverEnv& env)
{
    return env.saw_frame && env.saw_frame();
}

bool env_frame_requested(RenderTestDriverEnv& env)
{
    return env.frame_requested && env.frame_requested();
}

std::optional<std::chrono::steady_clock::time_point> diagnostics_panel_render_time(RenderTestDriverEnv& env)
{
    if (!env.diagnostics_panel_render_time)
        return std::nullopt;
    return env.diagnostics_panel_render_time();
}

void request_capture(RenderTestDriverEnv& env, ICaptureRenderer& capture)
{
    capture.request_frame_capture();
    if (env.request_frame)
        env.request_frame();
}

std::string timeout_error(RenderTestDriverEnv& env, const RenderTestContext& ctx)
{
    const auto host_state = active_host_state(env);
    if (!host_state)
        return "Timed out waiting for a stable render capture (no host)";

    const auto panel_time = diagnostics_panel_render_time(env);
    const bool post_diagnostics_frame = ctx.diagnostics_enabled_at && panel_time
        && *panel_time > *ctx.diagnostics_enabled_at;
    const auto diagnostics_age_ms = ctx.diagnostics_enabled_at
        ? std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - *ctx.diagnostics_enabled_at)
              .count()
        : -1;

    std::ostringstream oss;
    oss << "Timed out waiting for a stable render capture"
        << " (phase=" << render_test_phase_name(ctx.phase)
        << ", content_ready=" << (host_state->content_ready ? "true" : "false")
        << ", saw_frame=" << (env_saw_frame(env) ? "true" : "false")
        << ", frame_requested=" << (env_frame_requested(env) ? "true" : "false")
        << ", diagnostics_enabled=" << (ctx.diagnostics_enabled_at.has_value() ? "true" : "false")
        << ", capture_requested=" << (ctx.phase == RenderTestPhase::kCapturing ? "true" : "false")
        << ", post_diagnostics_frame=" << (post_diagnostics_frame ? "true" : "false")
        << ", diagnostics_age_ms=" << diagnostics_age_ms << ")";
    return oss.str();
}

} // namespace

RenderTestDriverResult run_render_test_driver(RenderTestDriverEnv& env, const RenderTestDriverOptions& options)
{
    RenderTestDriverResult result;
    ICaptureRenderer* capture = capture_renderer(env);
    if (!capture)
    {
        result.error = "Renderer does not support frame capture";
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    RenderTestContext ctx;

    while (env_is_running(env) && std::chrono::steady_clock::now() < deadline)
    {
        auto wait_deadline = deadline;
        if (ctx.phase == RenderTestPhase::kSettlingContent
            || ctx.phase == RenderTestPhase::kEnablingDiagnostics
            || ctx.phase == RenderTestPhase::kSettlingForCapture)
        {
            wait_deadline = std::min(wait_deadline, ctx.settle_start + options.settle);
        }

        if (env.pump_once)
            env.pump_once(wait_deadline);

        if (auto captured = capture->take_captured_frame())
        {
            result.frame = std::move(captured);
            return result;
        }

        const auto host_state = active_host_state(env);
        if (!host_state)
            continue;

        const auto now = std::chrono::steady_clock::now();
        const bool content_ready = host_state->content_ready && env_saw_frame(env);
        const bool content_quiet = content_ready && !env_frame_requested(env);

        switch (ctx.phase)
        {
        case RenderTestPhase::kWaitingForContent:
            if (content_ready)
            {
                ctx.settle_start = now;
                ctx.phase = RenderTestPhase::kSettlingContent;
            }
            break;

        case RenderTestPhase::kSettlingContent:
            if (!content_ready)
            {
                ctx.phase = RenderTestPhase::kWaitingForContent;
                break;
            }
            if (now - ctx.settle_start >= options.settle)
            {
                if (options.want_diagnostics)
                {
                    if (env.enable_diagnostics_panel)
                        env.enable_diagnostics_panel();
                    ctx.diagnostics_enabled_at = now;
                    ctx.settle_start = now;
                    ctx.phase = RenderTestPhase::kEnablingDiagnostics;
                }
                else
                {
                    if (content_quiet)
                        ctx.settle_start = now;
                    ctx.phase = RenderTestPhase::kSettlingForCapture;
                }
            }
            break;

        case RenderTestPhase::kEnablingDiagnostics:
        {
            const auto panel_time = diagnostics_panel_render_time(env);
            const bool panel_rendered = ctx.diagnostics_enabled_at && panel_time
                && *panel_time > *ctx.diagnostics_enabled_at;
            if (panel_rendered && now - ctx.settle_start >= options.settle)
            {
                request_capture(env, *capture);
                ctx.phase = RenderTestPhase::kCapturing;
            }
            break;
        }

        case RenderTestPhase::kSettlingForCapture:
            if (!content_quiet)
            {
                ctx.quiet_observed = false;
                break;
            }
            if (!ctx.quiet_observed)
            {
                ctx.quiet_observed = true;
                ctx.settle_start = now;
                break;
            }
            if (now - ctx.settle_start >= options.settle)
            {
                request_capture(env, *capture);
                ctx.phase = RenderTestPhase::kCapturing;
            }
            break;

        case RenderTestPhase::kCapturing:
            break;
        }
    }

    result.error = timeout_error(env, ctx);
    return result;
}

} // namespace draxul

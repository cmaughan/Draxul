#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <memory>

namespace draxul
{

class NanoVGDemoHost final : public IHost
{
public:
    NanoVGDemoHost() = default;

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport& viewport) override;
    void pump() override {}
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view /*action*/) override
    {
        return false;
    }
    void request_close() override
    {
        running_ = false;
    }
    Color default_background() const override
    {
        // draxul::Color is normalized floats; byte values clamp to white.
        return Color{ 0.157f, 0.173f, 0.204f, 1.0f };
    }
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

private:
    static void draw_demo(NVGcontext* vg, int w, int h);

    std::unique_ptr<INanoVGPass> nanovg_pass_;
    HostViewport viewport_;
    IHostCallbacks* callbacks_ = nullptr;
    bool running_ = false;
};

std::unique_ptr<IHost> create_nanovg_demo_host();

class HostProviderRegistry;
void register_nanovg_demo_host_provider(HostProviderRegistry& registry);

} // namespace draxul

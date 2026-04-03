#pragma once

#include "split_tree.h"

#include <draxul/base_renderer.h>
#include <draxul/host.h>
#include <draxul/nanovg_pass.h>

struct NVGcontext;

namespace draxul
{

// ChromeHost draws window chrome (pane dividers, future tab bar) using NanoVG.
// It is drawn first each frame, before content hosts, over the full window viewport.
// It does not own a grid or any subprocess — just vector graphics.
class ChromeHost final : public IHost
{
public:
    // The ChromeHost reads divider positions from the split tree each frame.
    explicit ChromeHost(const SplitTree& tree);

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
    void request_close() override {}
    Color default_background() const override
    {
        return { 0, 0, 0, 0 };
    }
    HostRuntimeState runtime_state() const override
    {
        return { true };
    }
    HostDebugState debug_state() const override
    {
        return { "chrome" };
    }

private:
    const SplitTree& tree_;
    std::unique_ptr<INanoVGPass> nanovg_pass_;
    HostViewport viewport_{};
    bool running_ = false;
};

} // namespace draxul

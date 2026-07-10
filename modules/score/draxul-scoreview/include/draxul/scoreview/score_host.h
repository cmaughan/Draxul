#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>

#include <memory>
#include <string>

namespace draxul
{

class HostProviderRegistry;

namespace scoreview
{

// ScoreHost — music score viewer (plans/scoreview.md).
//
// Phase 0: proof-of-life NanoVG scene — an empty page with a grand-staff
// placeholder drawn at SMuFL-ish engraving proportions. Later phases replace
// the placeholder with a Verovio-laid-out ScoreDrawList; the host shell
// (viewport, pass recording, source plumbing) stays as built here.
class ScoreHost final : public draxul::IHost
{
public:
    ScoreHost() = default;

    bool initialize(const draxul::HostContext& context, draxul::IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const draxul::HostViewport& viewport) override;
    void pump() override
    {
    }
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view /*action*/) override
    {
        return false;
    }
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;

private:
    std::unique_ptr<draxul::INanoVGPass> nanovg_pass_;
    draxul::HostViewport viewport_;
    draxul::IHostCallbacks* callbacks_ = nullptr;
    std::string source_path_;
    bool running_ = false;
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace scoreview
} // namespace draxul

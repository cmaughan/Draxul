#pragma once

#include <draxul/scoreview/score_runtime.h>

namespace draxul::scoreview
{

// Test-only/static parity adapter. Production startup uses the dynamic plugin
// route; this class keeps the existing orchestration tests useful while the
// runtime crosses that boundary.
class ScoreHost final : public draxul::IHost, public ScoreRuntime
{
public:
    bool initialize(const draxul::HostContext& context,
        draxul::IHostCallbacks& callbacks) override
    {
        callbacks_.target = &callbacks;
        return ScoreRuntime::initialize(context, callbacks_);
    }
    void shutdown() override
    {
        ScoreRuntime::shutdown();
        callbacks_.target = nullptr;
    }
    bool is_running() const override { return ScoreRuntime::is_running(); }
    std::string init_error() const override
    {
        return ScoreRuntime::init_error();
    }
    void set_viewport(const draxul::HostViewport& value) override
    {
        ScoreRuntime::set_viewport(value);
    }
    void on_config_reloaded(const draxul::HostReloadConfig& value) override
    {
        ScoreRuntime::on_config_reloaded(value);
    }
    void pump() override { ScoreRuntime::pump(); }
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline()
        const override
    {
        return ScoreRuntime::next_deadline();
    }
    void on_key(const draxul::KeyEvent& value) override
    {
        ScoreRuntime::on_key(value);
    }
    void on_mouse_wheel(const draxul::MouseWheelEvent& value) override
    {
        ScoreRuntime::on_mouse_wheel(value);
    }
    void on_mouse_button(const draxul::MouseButtonEvent& value) override
    {
        ScoreRuntime::on_mouse_button(value);
    }
    void on_mouse_move(const draxul::MouseMoveEvent& value) override
    {
        ScoreRuntime::on_mouse_move(value);
    }
    void on_text_input(const draxul::TextInputEvent& value) override
    {
        ScoreRuntime::on_text_input(value);
    }
    void attach_imgui_host(draxul::IImGuiHost& host) override
    {
        ScoreRuntime::attach_imgui_host(host);
    }
    void set_imgui_font(const std::string& path, float size) override
    {
        ScoreRuntime::set_imgui_font(path, size);
    }
    bool dispatch_action(std::string_view action) override
    {
        return ScoreRuntime::dispatch_action(action);
    }
    void request_close() override { ScoreRuntime::request_close(); }
    std::string status_text() const override
    {
        return ScoreRuntime::status_text();
    }
    draxul::Color default_background() const override
    {
        return ScoreRuntime::default_background();
    }
    draxul::HostRuntimeState runtime_state() const override
    {
        return ScoreRuntime::runtime_state();
    }
    draxul::HostDebugState debug_state() const override
    {
        return ScoreRuntime::debug_state();
    }
    draxul::HostPrintHint print_hint() const override
    {
        return ScoreRuntime::print_hint();
    }

private:
    class Callbacks final : public ScoreRuntimeCallbacks
    {
    public:
        void request_frame() override
        {
            if (target)
                target->request_frame();
        }
        draxul::IHostCallbacks* target = nullptr;
    } callbacks_;
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::scoreview

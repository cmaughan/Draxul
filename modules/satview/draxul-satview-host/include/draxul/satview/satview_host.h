#pragma once

#include <draxul/satview/satview_runtime.h>

namespace draxul::satview
{

// Static parity adapter. SatViewRuntime owns all product state and behavior;
// this class only translates Draxul's IHost virtual surface to that runtime.
class SatViewHost final : public draxul::IHost, public SatViewRuntime
{
public:
    bool initialize(const draxul::HostContext& context,
        draxul::IHostCallbacks& callbacks) override
    {
        callbacks_.target = &callbacks;
        return SatViewRuntime::initialize(context, callbacks_);
    }
    void shutdown() override
    {
        SatViewRuntime::shutdown();
        callbacks_.target = nullptr;
    }
    bool is_running() const override { return SatViewRuntime::is_running(); }
    std::string init_error() const override
    {
        return SatViewRuntime::init_error();
    }
    void set_viewport(const draxul::HostViewport& viewport) override
    {
        SatViewRuntime::set_viewport(viewport);
    }
    void on_font_metrics_changed() override
    {
        SatViewRuntime::on_font_metrics_changed();
    }
    void pump() override { SatViewRuntime::pump(); }
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline()
        const override
    {
        return SatViewRuntime::next_deadline();
    }
    void on_mouse_button(const draxul::MouseButtonEvent& event) override
    {
        SatViewRuntime::on_mouse_button(event);
    }
    void on_mouse_move(const draxul::MouseMoveEvent& event) override
    {
        SatViewRuntime::on_mouse_move(event);
    }
    void on_mouse_wheel(const draxul::MouseWheelEvent& event) override
    {
        SatViewRuntime::on_mouse_wheel(event);
    }
    void on_key(const draxul::KeyEvent& event) override
    {
        SatViewRuntime::on_key(event);
    }
    void on_text_input(const draxul::TextInputEvent& event) override
    {
        SatViewRuntime::on_text_input(event);
    }
    void on_focus_lost() override { SatViewRuntime::on_focus_lost(); }
    bool dispatch_action(std::string_view action) override
    {
        return SatViewRuntime::dispatch_action(action);
    }
    void request_close() override { SatViewRuntime::request_close(); }
    std::string status_text() const override
    {
        return SatViewRuntime::status_text();
    }
    draxul::Color default_background() const override
    {
        return SatViewRuntime::default_background();
    }
    draxul::HostRuntimeState runtime_state() const override
    {
        return SatViewRuntime::runtime_state();
    }
    draxul::HostDebugState debug_state() const override
    {
        return SatViewRuntime::debug_state();
    }
    void attach_imgui_host(draxul::IImGuiHost& host) override
    {
        SatViewRuntime::attach_imgui_host(host);
    }
    void set_imgui_font(const std::string& path, float size_pixels) override
    {
        SatViewRuntime::set_imgui_font(path, size_pixels);
    }

private:
    class Callbacks final : public SatViewRuntimeCallbacks
    {
    public:
        void request_frame() override
        {
            if (target)
                target->request_frame();
        }
        void request_quit() override
        {
            if (target)
                target->request_quit();
        }
        void set_window_title(std::string_view title) override
        {
            if (target)
                target->set_window_title(std::string(title));
        }

        draxul::IHostCallbacks* target = nullptr;
    } callbacks_;
};

std::unique_ptr<draxul::IHost> create_satview_host();

void register_satview_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::satview

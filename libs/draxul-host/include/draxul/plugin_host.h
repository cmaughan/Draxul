#pragma once

#include <draxul/host.h>
#include <draxul/plugin_api.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace draxul
{

class IRenderPass;
class LoadedPlugin;
class PluginManager;

class PluginHost final : public IHost
{
public:
    explicit PluginHost(std::shared_ptr<PluginManager> manager);
    ~PluginHost() override;

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override { return running_; }
    std::string init_error() const override { return error_; }
    std::string init_error_code() const override { return "plugin_load_failed"; }
    void set_viewport(const HostViewport& viewport) override;
    void set_presentation_visible(bool visible) override;
    bool requires_periodic_wake() const override { return false; }
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    void on_focus_gained() override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_text_editing(const TextEditingEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    bool dispatch_action(std::string_view) override { return false; }
    void request_close() override { running_ = false; }
    std::string display_name() const override;
    std::string status_text() const override { return error_; }
    Color default_background() const override { return { 0.04f, 0.05f, 0.08f, 1.0f }; }
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    void accept_render_result(const DraxulPluginRenderResultV1& result);

private:
    static void request_redraw(void* context);
    static void log_message(void* context, uint32_t level, const char* message, size_t length);
    DraxulPluginViewportV1 plugin_viewport() const;
    void send_input(DraxulPluginInputEventV1 event);
    void send_focus(bool focused);

    std::shared_ptr<PluginManager> manager_;
    std::shared_ptr<LoadedPlugin> plugin_;
    std::unique_ptr<IRenderPass> render_pass_;
    IGridRenderer* renderer_ = nullptr;
    std::atomic<IHostCallbacks*> callbacks_{ nullptr };
    void* instance_ = nullptr;
    HostViewport viewport_;
    DraxulPluginHostApiV1 host_api_{};
    std::atomic<bool> redraw_pending_{ false };
    std::optional<std::chrono::steady_clock::time_point> next_frame_;
    std::chrono::steady_clock::time_point started_at_{};
    std::string plugin_id_;
    std::string plugin_directory_;
    std::string config_json_;
    std::string error_;
    bool running_ = false;
    bool visible_ = true;
};

class HostProviderRegistry;
void register_plugin_host_provider(HostProviderRegistry& registry,
    std::shared_ptr<PluginManager> manager);

} // namespace draxul

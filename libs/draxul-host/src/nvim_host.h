#pragma once

#include <draxul/clipboard_poll_gate.h>
#include <draxul/grid_host_base.h>
#include <draxul/nvim.h>
#include <draxul/startup_resize_state.h>
#include <draxul/ui_request_worker.h>

#include <mutex>

namespace draxul
{

class NvimHost : public GridHostBase
{
public:
    bool is_running() const override;
    std::string init_error() const override;
    void shutdown() override;
    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_text_editing(const TextEditingEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    bool is_nvim_host() const override
    {
        return true;
    }
    std::string status_text() const override;

protected:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;
    std::string_view host_name() const override
    {
        return "nvim";
    }

private:
    bool attach_ui();
    bool apply_environment();
    bool execute_startup_commands();
    bool setup_clipboard_provider();
    void queue_resize_request(int cols, int rows, const char* reason);
    void on_flush();
    void on_busy(bool busy);
    void refresh_cursor_style();
    BlinkTiming current_blink_timing() const;
    MpackValue handle_rpc_request(const std::string& method, const std::vector<MpackValue>& params) const;
    void handle_clipboard_set(const std::vector<MpackValue>& params);
    void wire_ui_callbacks();
    void refresh_clipboard_cache();
    void refresh_clipboard_cache_if_due(ClipboardPollGate::Clock::time_point now);
    bool handle_copy_action(std::string_view action);
    bool handle_paste_action(std::string_view action);
    bool handle_open_file_at_function_action(std::string_view action);
    bool handle_open_file_at_type_action(std::string_view action);
    bool handle_open_file_action(std::string_view action);

    NvimProcess nvim_process_;
    NvimRpc rpc_;
    UiEventHandler ui_events_;
    NvimInput input_;
    UiRequestWorker ui_request_worker_;
    UiOptions ui_options_;
    StartupResizeState startup_resize_state_;
    int64_t clipboard_channel_id_ = -1;
    std::string init_error_;

    // Clipboard cache: refreshed on the main thread in pump(), read from the
    // reader thread in handle_rpc_request().  This avoids calling SDL clipboard
    // APIs (main-thread-only) from the reader thread.
    mutable std::mutex clipboard_mutex_;
    std::string clipboard_cache_;
    ClipboardPollGate clipboard_poll_gate_;
};

} // namespace draxul

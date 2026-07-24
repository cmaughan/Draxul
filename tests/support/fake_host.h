#pragma once

// WI 25 — canonical FakeHost for tests.
//
// A minimal IHost implementation suitable for unit tests that need a hostable
// pane without spawning Neovim, a shell, or any real grid renderer. It
// records calls that App/PaneManager/InputDispatcher perform against a host
// so tests can assert on routing behaviour, lifecycle ordering, and dispatch
// correctness.
//
// Typical uses:
//   - App lifecycle/smoke tests — exercise initialize/pump/shutdown sequencing.
//   - InputDispatcher routing tests — assert events land on the focused host.
//   - Dispatch-to-nvim tests — flip the is_nvim_host capability.
//
// Tests that need specialised behaviour (e.g. failing initialize, reload
// tracking) should subclass FakeHost and override just the method they care
// about. FakeHost itself exposes a small set of behaviour flags so common
// tweaks do not require a subclass.

#include <draxul/host.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul::tests
{

class FakeHost : public IHost
{
public:
    FakeHost() = default;
    explicit FakeHost(std::string debug_name)
        : debug_name_(std::move(debug_name))
    {
    }

    // ── Behaviour injection ─────────────────────────────────────────────────

    // When true, pump() calls callbacks_->request_frame() once per pump.
    // Matches the real-host contract where pumping produces output that
    // requires a repaint. Enabled by default so App smoke tests do not block
    // in wait_events().
    bool request_frame_on_pump = true;

    // When true, initialize() returns false and sets init_error.
    bool fail_initialize = false;
    std::string init_error_message;

    // Reported by is_nvim_host().
    bool is_nvim = false;

    // When true, shutdown() calls callbacks_->request_frame() before
    // returning. Exercises callback-during-teardown scenarios.
    bool fire_callback_on_shutdown = false;

    // Optional callback invoked from shutdown(). Useful for tests that need
    // to track shutdown *after* the host is destroyed (capture a shared_ptr
    // in the lambda so the counter outlives the host).
    std::function<void()> on_shutdown_callback;

    // ── IHost ───────────────────────────────────────────────────────────────

    bool initialize(const HostContext& ctx, IHostCallbacks& callbacks) override
    {
        ++initialize_calls;
        last_context_ = &ctx;
        callbacks_ = &callbacks;
        if (fail_initialize)
        {
            return false;
        }
        initialized_ = true;
        return true;
    }

    void shutdown() override
    {
        ++shutdown_calls;
        if (on_shutdown_callback)
            on_shutdown_callback();
        if (fire_callback_on_shutdown && callbacks_)
            callbacks_->request_frame();
        running_ = false;
    }

    // Test helper: trigger callbacks_->request_frame() from outside the host.
    // Used to verify the callback handle remains valid after sibling teardown.
    void trigger_frame_request() const
    {
        if (callbacks_)
            callbacks_->request_frame();
    }

    bool is_running() const override
    {
        return running_;
    }

    std::optional<int> exit_code() const override
    {
        return fake_exit_code;
    }

    std::string init_error() const override
    {
        return init_error_message;
    }

    void set_viewport(const HostViewport& viewport) override
    {
        ++set_viewport_calls;
        last_viewport = viewport;
    }

    void on_font_metrics_changed() override
    {
        ++font_metrics_changed_calls;
    }

    void on_config_reloaded(const HostReloadConfig& config) override
    {
        ++config_reloaded_calls;
        last_reload_config = config;
    }

    void pump() override
    {
        ++pump_calls;
        if (running_ && request_frame_on_pump && callbacks_)
            callbacks_->request_frame();
    }

    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    void on_focus_gained() override
    {
        ++focus_gained_calls;
    }

    void on_focus_lost() override
    {
        ++focus_lost_calls;
    }

    void on_key(const KeyEvent& event) override
    {
        key_events.push_back(event);
    }
    void on_text_input(const TextInputEvent& event) override
    {
        text_input_events.push_back(event);
    }
    void on_text_editing(const TextEditingEvent& event) override
    {
        text_editing_events.push_back(event);
    }
    void on_mouse_button(const MouseButtonEvent& event) override
    {
        mouse_button_events.push_back(event);
    }
    void on_mouse_move(const MouseMoveEvent& event) override
    {
        mouse_move_events.push_back(event);
    }
    void on_mouse_wheel(const MouseWheelEvent& event) override
    {
        mouse_wheel_events.push_back(event);
    }

    bool dispatch_action(std::string_view action) override
    {
        dispatched_actions.emplace_back(action);
        return dispatch_action_result;
    }

    void request_close() override
    {
        ++request_close_calls;
        running_ = false;
    }

    bool is_nvim_host() const override
    {
        return is_nvim;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = initialized_;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = debug_name_;
        return state;
    }

    // ── Test introspection / mutators ───────────────────────────────────────

    IHostCallbacks* callbacks() const
    {
        return callbacks_;
    }
    const HostContext* last_context() const
    {
        return last_context_;
    }
    bool was_initialized() const
    {
        return initialized_;
    }
    void set_debug_name(std::string name)
    {
        debug_name_ = std::move(name);
    }

    // Controls what dispatch_action returns; tests can flip to false to
    // simulate an action the host does not recognise.
    bool dispatch_action_result = true;
    std::optional<int> fake_exit_code;

    // Recorded state.
    int initialize_calls = 0;
    int shutdown_calls = 0;
    int pump_calls = 0;
    int set_viewport_calls = 0;
    int font_metrics_changed_calls = 0;
    int config_reloaded_calls = 0;
    int focus_gained_calls = 0;
    int focus_lost_calls = 0;
    int request_close_calls = 0;

    HostViewport last_viewport{};
    HostReloadConfig last_reload_config{};

    std::vector<KeyEvent> key_events;
    std::vector<TextInputEvent> text_input_events;
    std::vector<TextEditingEvent> text_editing_events;
    std::vector<MouseButtonEvent> mouse_button_events;
    std::vector<MouseMoveEvent> mouse_move_events;
    std::vector<MouseWheelEvent> mouse_wheel_events;
    std::vector<std::string> dispatched_actions;

protected:
    IHostCallbacks* callbacks_ = nullptr;
    const HostContext* last_context_ = nullptr;
    bool initialized_ = false;
    bool running_ = true;
    std::string debug_name_ = "fake-host";
};

} // namespace draxul::tests

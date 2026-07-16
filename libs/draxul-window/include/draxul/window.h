#pragma once
#include <draxul/events.h>
#include <draxul/types.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace draxul
{

class IWindow
{
private:
    enum class CallbackGroup
    {
        Input,
        Lifecycle,
    };

    struct CallbackLifetime
    {
        std::atomic<bool> connected{ true };
    };

    struct CallbackState;

public:
    struct InputCallbacks
    {
        std::function<void(const WindowResizeEvent&)> on_resize;
        std::function<void(const DisplayScaleEvent&)> on_display_scale_changed;
        std::function<void(const KeyEvent&)> on_key;
        std::function<void(const TextInputEvent&)> on_text_input;
        std::function<void(const TextEditingEvent&)> on_text_editing;
        std::function<void(const MouseButtonEvent&)> on_mouse_button;
        std::function<void(const MouseMoveEvent&)> on_mouse_move;
        std::function<void(const MouseWheelEvent&)> on_mouse_wheel;
        std::function<void(std::string_view path)> on_drop_file;
    };

    struct LifecycleCallbacks
    {
        std::function<void()> on_close_requested;
        std::function<void()> on_quit_requested;
        std::function<void()> on_dock_reopen;
    };

    class CallbackConnection
    {
    public:
        CallbackConnection() = default;
        ~CallbackConnection();
        CallbackConnection(const CallbackConnection&) = delete;
        CallbackConnection& operator=(const CallbackConnection&) = delete;
        CallbackConnection(CallbackConnection&& other) noexcept;
        CallbackConnection& operator=(CallbackConnection&& other) noexcept;

        void reset();
        [[nodiscard]] bool connected() const
        {
            return lifetime_ && lifetime_->connected.load(std::memory_order_acquire);
        }

    private:
        friend class IWindow;
        CallbackConnection(std::weak_ptr<CallbackState> state, CallbackGroup group,
            std::uint64_t generation, std::shared_ptr<CallbackLifetime> lifetime);

        std::weak_ptr<CallbackState> state_;
        CallbackGroup group_ = CallbackGroup::Input;
        std::uint64_t generation_ = 0;
        std::shared_ptr<CallbackLifetime> lifetime_;
    };

    IWindow();
    virtual ~IWindow();
    IWindow(const IWindow&) = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow(IWindow&&) = delete;
    IWindow& operator=(IWindow&&) = delete;
    virtual bool initialize(const std::string& title, int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual bool poll_events() = 0; // returns false if quit requested
    virtual void* native_handle() = 0; // NOSONAR cpp:S5008
    virtual std::pair<int, int> size_logical() const = 0;
    virtual std::pair<int, int> size_pixels() const = 0;
    virtual int width_pixels() const
    {
        auto [w, h] = size_pixels();
        return w;
    }
    virtual int height_pixels() const
    {
        auto [w, h] = size_pixels();
        return h;
    }
    virtual int width_logical() const
    {
        auto [w, h] = size_logical();
        return w;
    }
    virtual int height_logical() const
    {
        auto [w, h] = size_logical();
        return h;
    }
    virtual float display_ppi() const = 0; // Physical pixels per inch of the display
    virtual void set_title(const std::string& title) = 0;
    virtual std::string clipboard_text() const = 0;
    virtual bool set_clipboard_text(const std::string& text) = 0;
    virtual bool open_url(std::string_view)
    {
        return false;
    }
    virtual void set_text_input_area(int x, int y, int w, int h) = 0;
    virtual void show()
    {
        // Default no-op; platform backends override when they can show the window.
    }
    virtual void hide()
    {
        // Default no-op; platform backends override when they can hide the window.
    }
    virtual bool is_visible() const
    {
        return true;
    }
    virtual void normalize_render_target_window_size(int /*target_pixel_width*/, int /*target_pixel_height*/)
    {
        // Default no-op; window backends override when render tests need size normalization.
    }

    // Wake the event loop from another thread (e.g. after a host requests a frame).
    virtual void wake()
    {
        // Default no-op; platform backends override when they can wake a blocked event loop.
    }
    // Bring the window to the foreground and give it input focus.
    virtual void activate()
    {
        // Default no-op; platform backends override when they can request focus.
    }
    // Block until an event arrives or timeout_ms elapses. Returns false if quit.
    virtual bool wait_events(int /*timeout_ms*/)
    {
        return true;
    }
    // Hint to clamp the window position to display bounds.
    virtual void set_clamp_to_display(bool)
    {
        // Default no-op; platform backends may use this hint during window creation.
    }
    // Hint to create the window hidden (used for headless render tests).
    virtual void set_hidden(bool)
    {
        // Default no-op; platform backends may use this hint during window creation.
    }

    // Show the platform native file-open dialog. Non-blocking: when the user
    // picks a file (or cancels), on_drop_file is called with the path (or no
    // call is made on cancel). The default implementation does nothing.
    virtual void show_open_file_dialog()
    {
        // Default no-op; override in platform implementations.
    }

    // Set the system mouse cursor shape. Backends map MouseCursor enum values
    // to native cursors. Default no-op for backends without cursor support.
    virtual void set_mouse_cursor(MouseCursor)
    {
        // Default no-op; override in platform implementations.
    }

    // Tint the OS title bar to match the given background color.
    // Best-effort: silently ignored on platforms/versions that don't support it.
    virtual void set_title_bar_color(Color)
    {
        // Default no-op; override in platform implementations.
    }

    // Installs one owner-controlled callback group. The returned token clears
    // the group when reset or destroyed. Copies of installed callbacks also
    // become inert when the token/window is cleared, making queued late events
    // safe even after the callback target has begun teardown.
    [[nodiscard]] CallbackConnection connect_input_callbacks(InputCallbacks callbacks);
    [[nodiscard]] CallbackConnection connect_lifecycle_callbacks(LifecycleCallbacks callbacks);
    virtual void clear_input_callbacks();
    virtual void clear_lifecycle_callbacks();
    virtual void clear_callbacks();

    // Callbacks
    std::function<void(const WindowResizeEvent&)> on_resize;
    std::function<void(const DisplayScaleEvent&)> on_display_scale_changed;
    std::function<void(const KeyEvent&)> on_key;
    std::function<void(const TextInputEvent&)> on_text_input;
    std::function<void(const TextEditingEvent&)> on_text_editing;
    std::function<void(const MouseButtonEvent&)> on_mouse_button;
    std::function<void(const MouseMoveEvent&)> on_mouse_move;
    std::function<void(const MouseWheelEvent&)> on_mouse_wheel;
    std::function<void()> on_close_requested;
    // Fired on Cmd+Q / menu Quit / Dock right-click Quit. Unlike close_requested
    // (which may detach), this always terminates the application.
    std::function<void()> on_quit_requested;
    // macOS: fired when the user clicks the Dock icon while no windows are visible.
    // The handler should create/show a window (e.g. reattach a session).
    std::function<void()> on_dock_reopen;
    // Fired when a file is dropped onto the window or chosen via the open dialog.
    std::function<void(std::string_view path)> on_drop_file;

private:
    struct CallbackState
    {
        explicit CallbackState(IWindow* owner_in)
            : owner(owner_in)
        {
        }

        IWindow* owner = nullptr;
        std::uint64_t input_generation = 0;
        std::uint64_t lifecycle_generation = 0;
        std::weak_ptr<CallbackLifetime> input_lifetime;
        std::weak_ptr<CallbackLifetime> lifecycle_lifetime;
    };

    template <typename... Args>
    static std::function<void(Args...)> guarded_callback(
        std::function<void(Args...)> callback,
        const std::shared_ptr<CallbackLifetime>& lifetime)
    {
        if (!callback)
            return {};
        std::weak_ptr<CallbackLifetime> weak_lifetime = lifetime;
        return [weak_lifetime, callback = std::move(callback)](Args... args) {
            const auto locked = weak_lifetime.lock();
            if (!locked || !locked->connected.load(std::memory_order_acquire))
                return;
            callback(std::forward<Args>(args)...);
        };
    }

    std::shared_ptr<CallbackState> callback_state_;
};

inline IWindow::IWindow()
    : callback_state_(std::make_shared<CallbackState>(this))
{
}

inline IWindow::~IWindow()
{
    clear_callbacks();
    callback_state_->owner = nullptr;
}

inline IWindow::CallbackConnection::CallbackConnection(
    std::weak_ptr<CallbackState> state, CallbackGroup group,
    std::uint64_t generation, std::shared_ptr<CallbackLifetime> lifetime)
    : state_(std::move(state))
    , group_(group)
    , generation_(generation)
    , lifetime_(std::move(lifetime))
{
}

inline IWindow::CallbackConnection::~CallbackConnection()
{
    reset();
}

inline IWindow::CallbackConnection::CallbackConnection(CallbackConnection&& other) noexcept
    : state_(std::move(other.state_))
    , group_(other.group_)
    , generation_(other.generation_)
    , lifetime_(std::move(other.lifetime_))
{
    other.generation_ = 0;
}

inline IWindow::CallbackConnection& IWindow::CallbackConnection::operator=(CallbackConnection&& other) noexcept
{
    if (this == &other)
        return *this;
    reset();
    state_ = std::move(other.state_);
    group_ = other.group_;
    generation_ = other.generation_;
    lifetime_ = std::move(other.lifetime_);
    other.generation_ = 0;
    return *this;
}

inline void IWindow::CallbackConnection::reset()
{
    if (lifetime_)
        lifetime_->connected.store(false, std::memory_order_release);
    if (const auto state = state_.lock(); state && state->owner)
    {
        if (group_ == CallbackGroup::Input && state->input_generation == generation_)
            state->owner->clear_input_callbacks();
        else if (group_ == CallbackGroup::Lifecycle && state->lifecycle_generation == generation_)
            state->owner->clear_lifecycle_callbacks();
    }
    lifetime_.reset();
    state_.reset();
    generation_ = 0;
}

inline IWindow::CallbackConnection IWindow::connect_input_callbacks(InputCallbacks callbacks)
{
    clear_input_callbacks();
    const auto lifetime = std::make_shared<CallbackLifetime>();
    callback_state_->input_lifetime = lifetime;
    on_resize = guarded_callback(std::move(callbacks.on_resize), lifetime);
    on_display_scale_changed = guarded_callback(std::move(callbacks.on_display_scale_changed), lifetime);
    on_key = guarded_callback(std::move(callbacks.on_key), lifetime);
    on_text_input = guarded_callback(std::move(callbacks.on_text_input), lifetime);
    on_text_editing = guarded_callback(std::move(callbacks.on_text_editing), lifetime);
    on_mouse_button = guarded_callback(std::move(callbacks.on_mouse_button), lifetime);
    on_mouse_move = guarded_callback(std::move(callbacks.on_mouse_move), lifetime);
    on_mouse_wheel = guarded_callback(std::move(callbacks.on_mouse_wheel), lifetime);
    on_drop_file = guarded_callback(std::move(callbacks.on_drop_file), lifetime);
    return CallbackConnection(callback_state_, CallbackGroup::Input,
        callback_state_->input_generation, lifetime);
}

inline IWindow::CallbackConnection IWindow::connect_lifecycle_callbacks(LifecycleCallbacks callbacks)
{
    clear_lifecycle_callbacks();
    const auto lifetime = std::make_shared<CallbackLifetime>();
    callback_state_->lifecycle_lifetime = lifetime;
    on_close_requested = guarded_callback(std::move(callbacks.on_close_requested), lifetime);
    on_quit_requested = guarded_callback(std::move(callbacks.on_quit_requested), lifetime);
    on_dock_reopen = guarded_callback(std::move(callbacks.on_dock_reopen), lifetime);
    return CallbackConnection(callback_state_, CallbackGroup::Lifecycle,
        callback_state_->lifecycle_generation, lifetime);
}

inline void IWindow::clear_input_callbacks()
{
    if (const auto lifetime = callback_state_->input_lifetime.lock())
        lifetime->connected.store(false, std::memory_order_release);
    callback_state_->input_lifetime.reset();
    ++callback_state_->input_generation;
    on_resize = {};
    on_display_scale_changed = {};
    on_key = {};
    on_text_input = {};
    on_text_editing = {};
    on_mouse_button = {};
    on_mouse_move = {};
    on_mouse_wheel = {};
    on_drop_file = {};
}

inline void IWindow::clear_lifecycle_callbacks()
{
    if (const auto lifetime = callback_state_->lifecycle_lifetime.lock())
        lifetime->connected.store(false, std::memory_order_release);
    callback_state_->lifecycle_lifetime.reset();
    ++callback_state_->lifecycle_generation;
    on_close_requested = {};
    on_quit_requested = {};
    on_dock_reopen = {};
}

inline void IWindow::clear_callbacks()
{
    clear_input_callbacks();
    clear_lifecycle_callbacks();
}

} // namespace draxul

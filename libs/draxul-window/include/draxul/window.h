#pragma once
#include <draxul/events.h>
#include <draxul/types.h>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace draxul
{

class IWindow
{
public:
    virtual ~IWindow() = default;
    virtual bool initialize(const std::string& title, int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual bool poll_events() = 0; // returns false if quit requested
    virtual void* native_handle() = 0; // NOSONAR cpp:S5008
    virtual std::pair<int, int> size_logical() const = 0;
    virtual std::pair<int, int> size_pixels() const = 0;
    virtual float display_ppi() const = 0; // Physical pixels per inch of the display
    virtual void set_title(const std::string& title) = 0;
    virtual std::string clipboard_text() const = 0;
    virtual bool set_clipboard_text(const std::string& text) = 0;
    virtual void set_text_input_area(int x, int y, int w, int h) = 0;
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

    // Tint the OS title bar to match the given background color.
    // Best-effort: silently ignored on platforms/versions that don't support it.
    virtual void set_title_bar_color(Color)
    {
        // Default no-op; override in platform implementations.
    }

    // Callbacks
    std::function<void(const WindowResizeEvent&)> on_resize;
    std::function<void(const DisplayScaleEvent&)> on_display_scale_changed;
    std::function<void(const KeyEvent&)> on_key;
    std::function<void(const TextInputEvent&)> on_text_input;
    std::function<void(const TextEditingEvent&)> on_text_editing;
    std::function<void(const MouseButtonEvent&)> on_mouse_button;
    std::function<void(const MouseMoveEvent&)> on_mouse_move;
    std::function<void(const MouseWheelEvent&)> on_mouse_wheel;
    // Fired when a file is dropped onto the window or chosen via the open dialog.
    std::function<void(std::string_view path)> on_drop_file;
};

} // namespace draxul

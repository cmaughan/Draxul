#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace draxul
{

// Encodes and emits mouse-protocol escape sequences on behalf of
// TerminalHostBase.  The host feeds button and motion events in;
// MouseReporter formats the appropriate X10 or SGR escape sequence and
// delivers it via the write callback.
class MouseReporter
{
public:
    enum class MouseMode
    {
        None,
        Button,
        Drag,
        All,
    };

    // write_fn is called when a mouse-report sequence must be sent to the
    // process.  The callback owns the string and must not store the view.
    using WriteFn = std::function<void(std::string_view)>;

    explicit MouseReporter(WriteFn write_fn);

    // DECSET mode setters (called from handle_csi '?h'/'?l').
    void set_mode(int decset_mode, bool enable);

    // Current mode query (used by TerminalHostBase to decide whether to
    // handle scroll-wheel itself or pass it to the reporter).
    MouseMode mode() const
    {
        return mouse_mode_;
    }

    // Process a button press/release.  Returns true if the event was consumed
    // by the reporter (i.e. a sequence was sent and the caller should skip its
    // own selection handling).
    bool on_button(int button, bool pressed, int mod_bits, int col, int row);

    // Process cursor motion.  Returns true if consumed by the reporter.
    bool on_move(int col, int row);

    // Process scroll-wheel events.
    void on_wheel(int button_code, int col, int row);

    // Reset all state (called from reset_terminal_state).
    void reset();

private:
    void send_report(int button_code, bool pressed, int col, int row) const;

    WriteFn write_fn_;
    MouseMode mouse_mode_ = MouseMode::None;
    bool mouse_sgr_ = false;
    uint8_t mouse_btn_pressed_ = 0; // bitmask: bit 0=left, bit 1=middle, bit 2=right
};

} // namespace draxul

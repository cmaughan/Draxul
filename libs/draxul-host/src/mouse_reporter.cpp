#include <draxul/mouse_reporter.h>

#include <algorithm>
#include <cstdio>
#include <draxul/input_types.h>
#include <string>

namespace draxul
{

MouseReporter::MouseReporter(WriteFn write_fn)
    : write_fn_(std::move(write_fn))
{
}

void MouseReporter::set_mode(int decset_mode, bool enable)
{
    switch (decset_mode)
    {
    case 1000: // X10/Normal tracking
        mouse_mode_ = enable ? MouseMode::Button : MouseMode::None;
        break;
    case 1002: // Button motion
        mouse_mode_ = enable ? MouseMode::Drag : MouseMode::None;
        break;
    case 1003: // Any motion
        mouse_mode_ = enable ? MouseMode::All : MouseMode::None;
        break;
    case 1006: // SGR extended format
        mouse_sgr_ = enable;
        break;
    default:
        break;
    }
}

bool MouseReporter::on_button(int button, bool pressed, int mod_bits, int col, int row)
{
    // Track which buttons are held for DECSET 1002/1003 motion reporting.
    const int btn_idx = button - 1;
    if (btn_idx >= 0 && btn_idx <= 2)
    {
        if (pressed)
            mouse_btn_pressed_ |= static_cast<uint8_t>(1u << btn_idx);
        else
            mouse_btn_pressed_ &= ~static_cast<uint8_t>(1u << btn_idx);
    }

    if (mouse_mode_ == MouseMode::None)
        return false;

    int button_code = button - 1;
    if (button_code < 0 || button_code > 2)
        return false;

    if (mod_bits & static_cast<int>(kModShift))
        button_code |= 4;
    if (mod_bits & static_cast<int>(kModAlt))
        button_code |= 8;
    if (mod_bits & static_cast<int>(kModCtrl))
        button_code |= 16;
    if (!pressed)
        button_code = 3; // release code for non-SGR; SGR uses final char 'm'

    send_report(button_code, pressed, col, row);
    return true;
}

bool MouseReporter::on_move(int col, int row)
{
    // Button mode (1000) only reports press/release, never motion.
    if (mouse_mode_ == MouseMode::None || mouse_mode_ == MouseMode::Button)
        return false;
    if (mouse_mode_ != MouseMode::All && mouse_btn_pressed_ == 0)
        return false;

    // Motion with button held: button_code = 32 + button (left=0 → 32).
    // Use the lowest set button bit to determine which button is held.
    int held_btn = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (mouse_btn_pressed_ & (1u << i))
        {
            held_btn = i;
            break;
        }
    }
    // For any-motion mode with no button held, use 35 (no button).
    const int button_code = (mouse_mode_ == MouseMode::All && mouse_btn_pressed_ == 0)
        ? 35
        : 32 + held_btn;
    send_report(button_code, true, col, row);
    return true;
}

void MouseReporter::on_wheel(int button_code, int col, int row)
{
    send_report(button_code, true, col, row);
}

void MouseReporter::reset()
{
    mouse_mode_ = MouseMode::None;
    mouse_sgr_ = false;
    mouse_btn_pressed_ = 0;
}

void MouseReporter::send_report(int button_code, bool pressed, int col, int row) const
{
    if (mouse_sgr_)
    {
        std::string seq(32, '\0');
        const int len = snprintf(seq.data(), seq.size(), "\x1B[<%d;%d;%d%c",
            button_code, col + 1, row + 1, pressed ? 'M' : 'm');
        if (len > 0)
            write_fn_(std::string_view(seq.data(), static_cast<size_t>(len)));
    }
    else if (col < 223 && row < 223)
    {
        std::string seq(6, '\0');
        seq[0] = '\x1B';
        seq[1] = '[';
        seq[2] = 'M';
        seq[3] = static_cast<char>(button_code + 32);
        seq[4] = static_cast<char>(col + 33);
        seq[5] = static_cast<char>(row + 33);
        write_fn_(std::string_view(seq.data(), seq.size()));
    }
}

} // namespace draxul

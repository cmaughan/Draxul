#include <draxul/scoreview/keyboard_render_nvg.h>

namespace draxul
{
namespace scoreview
{

bool keyboard_is_black(int midi)
{
    const int pc = ((midi % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

int keyboard_white_index(int midi)
{
    if (midi < kKeyboardLowMidi || midi > kKeyboardHighMidi || keyboard_is_black(midi))
        return -1;
    int index = 0;
    for (int at = kKeyboardLowMidi; at < midi; ++at)
        index += keyboard_is_black(at) ? 0 : 1;
    return index;
}

float keyboard_key_center_x(int midi, float x, float w)
{
    const float white_w = w / static_cast<float>(kKeyboardWhiteKeys);
    if (!keyboard_is_black(midi))
        return x + (static_cast<float>(keyboard_white_index(midi)) + 0.5f) * white_w;
    // A black key sits on the boundary between its white neighbours.
    const int left_white = midi - 1; // the white below every black key
    return x + static_cast<float>(keyboard_white_index(left_white) + 1) * white_w;
}

} // namespace scoreview
} // namespace draxul

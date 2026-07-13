#pragma once

// The guidance keyboard (stream plan follow-up): a full-width 88-key piano
// drawn under the score. Keys the player still needs help with light up;
// the whole keyboard fades away as the material is proven. Pure NanoVG
// drawing plus small pure geometry helpers (unit-tested).

#include <vector>

struct NVGcontext;

namespace draxul
{
namespace scoreview
{

// 88 keys, A0 (midi 21) .. C8 (midi 108): 52 white keys.
constexpr int kKeyboardLowMidi = 21;
constexpr int kKeyboardHighMidi = 108;
constexpr int kKeyboardWhiteKeys = 52;

bool keyboard_is_black(int midi);
// White-key index 0..51 for white midis; -1 for black keys.
int keyboard_white_index(int midi);
// Horizontal center of a key within a keyboard spanning [x, x+w).
float keyboard_key_center_x(int midi, float x, float w);

// The pairing palette: one color per PITCH CLASS — C is always the same
// color on every staff, key, and octave; C# always its own; and so on
// around the chromatic circle. Hues are spread and toned to read on the
// white sheet, white keys, and black keys, deliberately skipping the
// verdict pure-red and pure-green.
constexpr int kGuidancePaletteSize = 12;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 43, 111, 221 }, // C   blue
    { 95, 91, 219 }, // C#  indigo
    { 142, 79, 209 }, // D   violet
    { 187, 63, 180 }, // D#  purple-magenta
    { 214, 51, 132 }, // E   pink
    { 217, 107, 42 }, // F   orange
    { 201, 138, 26 }, // F#  amber
    { 143, 143, 22 }, // G   olive
    { 91, 166, 42 }, // G#  yellow-green
    { 23, 160, 119 }, // A   teal
    { 15, 149, 168 }, // A#  cyan
    { 26, 130, 196 }, // B   azure
};
inline int guidance_palette_index(int midi)
{
    return ((midi % 12) + 12) % 12; // pitch class
}

struct KeyboardLit
{
    int midi = -1;
    float alpha = 1.0f; // per-key guidance strength, 0..1
    int palette = 0; // kGuidancePalette index shared with the sheet
};

// Draws the keyboard into [x, y, w, h] at `overall_alpha` (0 = invisible,
// callers can skip the call entirely at 0), lighting `lit` keys.
void draw_piano_keyboard(NVGcontext* vg, float x, float y, float w, float h,
    const std::vector<KeyboardLit>& lit, float overall_alpha, float pixel_scale);

} // namespace scoreview
} // namespace draxul

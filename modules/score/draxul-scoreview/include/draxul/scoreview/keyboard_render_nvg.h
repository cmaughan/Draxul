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

// The pairing palette: upcoming notes on the sheet and their keys share a
// color (stable per key: index = midi mod size, so octaves match). Chosen
// to read on the white sheet, white keys, and black keys, and to avoid the
// verdict green/red.
constexpr int kGuidancePaletteSize = 6;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 31, 119, 180 }, // blue
    { 217, 95, 2 }, // orange
    { 117, 112, 179 }, // violet
    { 231, 41, 138 }, // magenta
    { 27, 158, 119 }, // sea green
    { 166, 118, 29 }, // ochre
};
inline int guidance_palette_index(int midi)
{
    return ((midi % kGuidancePaletteSize) + kGuidancePaletteSize) % kGuidancePaletteSize;
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

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

// The pairing palette: one color per NOTE SPELLING, indexed
// letter*3 + (sign+1) (letter C=0..B=6, accidental flat 0 / natural 1 /
// sharp 2). The seven NATURALS (the white keys) use the BOOMWHACKERS
// colors learners already know — C red, D orange, E yellow, F green,
// G teal, A blue, B magenta — a full rainbow, so every white key is its
// own hue (C and F on opposite sides, no longer alike). Each black key
// takes the hue between its neighbours; its SHARP spelling is a brighter,
// warmer variant leaning toward the lower natural, its FLAT a darker,
// cooler variant leaning toward the upper — so C# and Db read apart, and
// every accidental sits a clear lightness step below the naturals. Tuned
// to read on the white sheet, white keys, and black keys (min perceptual
// gap ~18 dE across the common spellings, ~36 among the white keys).
constexpr int kGuidancePaletteSize = 21;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 147, 53, 119 }, //  0 Cb
    { 222, 49, 43 }, //  1 C   red        (Boomwhacker)
    { 202, 90, 58 }, //  2 C#  warm orange
    { 146, 66, 25 }, //  3 Db  deep amber
    { 240, 126, 32 }, //  4 D   orange     (Boomwhacker)
    { 176, 109, 22 }, //  5 D#  warm ochre
    { 120, 82, 0 }, //  6 Eb  deep ochre
    { 233, 190, 25 }, //  7 E   yellow     (Boomwhacker)
    { 81, 138, 39 }, //  8 E#
    { 109, 87, 0 }, //  9 Fb
    { 106, 178, 54 }, // 10 F   green      (Boomwhacker)
    { 5, 143, 69 }, // 11 F#  warm green
    { 0, 102, 78 }, // 12 Gb  deep green
    { 26, 175, 165 }, // 13 G   teal       (Boomwhacker)
    { 13, 136, 158 }, // 14 G#  warm cyan
    { 0, 96, 138 }, // 15 Ab  deep cyan
    { 52, 96, 200 }, // 16 A   blue       (Boomwhacker)
    { 139, 106, 205 }, // 17 A#  warm violet
    { 128, 64, 139 }, // 18 Bb  deep violet
    { 198, 66, 160 }, // 19 B   magenta    (Boomwhacker)
    { 207, 85, 69 }, // 20 B#
};

// Fallback spelling when the notated letter is unknown (stray notes, or a
// note absent from the engraving): the sharp reading of the pitch class.
inline int guidance_default_letter(int midi)
{
    static constexpr int kByPitchClass[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    return kByPitchClass[((midi % 12) + 12) % 12];
}

// Palette index for a note from its MIDI pitch and notated diatonic letter
// (0=C .. 6=B, or -1 when unknown — then the pitch class picks a default
// spelling). The letter is what tells C# (letter C) apart from Db (letter
// D): same key, same MIDI, different color.
inline int guidance_palette_index(int midi, int letter = -1)
{
    if (letter < 0 || letter > 6)
        letter = guidance_default_letter(midi);
    static constexpr int kNaturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int alter = (((midi % 12) + 12) % 12) - kNaturalPc[letter];
    if (alter > 6)
        alter -= 12;
    if (alter < -6)
        alter += 12;
    const int sign = alter < 0 ? -1 : (alter > 0 ? 1 : 0);
    return letter * 3 + (sign + 1);
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

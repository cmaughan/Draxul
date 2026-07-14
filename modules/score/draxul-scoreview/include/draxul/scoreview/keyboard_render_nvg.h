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
// sharp 2). The seven NATURALS (the white keys) are spread evenly around
// the wheel in CIELAB so every white key is its own hue — C red, D gold,
// E green, F teal, G cyan, A blue, B magenta — with C and F on opposite
// sides (they no longer look alike). Each black key takes the hue between
// its neighbours; its SHARP spelling is a brighter, warmer variant leaning
// toward the lower natural, its FLAT a darker, cooler variant leaning
// toward the upper — so C# and Db read apart, and every accidental sits a
// clear lightness step below the naturals. Tuned to read on the white
// sheet, white keys, and black keys (min perceptual gap ~15 dE across the
// common spellings, ~34 among the white keys).
constexpr int kGuidancePaletteSize = 21;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 140, 52, 125 }, //  0 Cb
    { 250, 99, 105 }, //  1 C   red
    { 201, 85, 54 }, //  2 C#  warm orange
    { 138, 68, 7 }, //  3 Db  deep amber
    { 200, 138, 26 }, //  4 D   gold
    { 140, 121, 0 }, //  5 D#  warm olive
    { 80, 93, 0 }, //  6 Eb  deep olive
    { 104, 166, 52 }, //  7 E   green
    { 11, 137, 120 }, //  8 E#
    { 50, 99, 7 }, //  9 Fb
    { 11, 169, 148 }, // 10 F   teal
    { 6, 135, 136 }, // 11 F#  warm teal
    { 9, 96, 107 }, // 12 Gb  deep teal
    { 3, 163, 198 }, // 13 G   cyan
    { 0, 131, 176 }, // 14 G#  warm blue
    { 8, 91, 141 }, // 15 Ab  deep blue
    { 84, 148, 254 }, // 16 A   blue
    { 117, 108, 211 }, // 17 A#  warm violet
    { 117, 64, 145 }, // 18 Bb  deep violet
    { 223, 107, 201 }, // 19 B   magenta
    { 211, 74, 81 }, // 20 B#
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

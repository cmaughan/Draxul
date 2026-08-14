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

// The pairing palette: indexed letter*3 + (sign+1) (letter C=0..B=6,
// accidental flat 0 / natural 1 / sharp 2). The seven NATURALS (the white
// keys) use the BOOMWHACKERS colors learners already know — C red, D orange,
// E yellow, F green, G teal, A blue, B magenta. An ACCIDENTAL wears its
// parent letter's exact color (F# = F green, Gb = G teal); the half-moon
// notehead shape — not a hue shift — is what marks it as sharp/flat, so
// C# (a C, red) and Db (a D, orange) still read apart by letter. The index
// still encodes the sign (for the half-moon's side), so all three entries
// of a letter simply share that letter's color.
constexpr int kGuidancePaletteSize = 21;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 222, 49, 43 }, //  0 Cb  = C
    { 222, 49, 43 }, //  1 C   red        (Boomwhacker)
    { 222, 49, 43 }, //  2 C#  = C
    { 240, 126, 32 }, //  3 Db  = D
    { 240, 126, 32 }, //  4 D   orange     (Boomwhacker)
    { 240, 126, 32 }, //  5 D#  = D
    { 233, 190, 25 }, //  6 Eb  = E
    { 233, 190, 25 }, //  7 E   yellow     (Boomwhacker)
    { 233, 190, 25 }, //  8 E#  = E
    { 106, 178, 54 }, //  9 Fb  = F
    { 106, 178, 54 }, // 10 F   green      (Boomwhacker)
    { 106, 178, 54 }, // 11 F#  = F
    { 26, 175, 165 }, // 12 Gb  = G
    { 26, 175, 165 }, // 13 G   teal       (Boomwhacker)
    { 26, 175, 165 }, // 14 G#  = G
    { 52, 96, 200 }, // 15 Ab  = A
    { 52, 96, 200 }, // 16 A   blue       (Boomwhacker)
    { 52, 96, 200 }, // 17 A#  = A
    { 198, 66, 160 }, // 18 Bb  = B
    { 198, 66, 160 }, // 19 B   magenta    (Boomwhacker)
    { 198, 66, 160 }, // 20 B#  = B
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

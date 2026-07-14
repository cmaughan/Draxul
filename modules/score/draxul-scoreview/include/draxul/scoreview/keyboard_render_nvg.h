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

// The pairing palette: one color per NOTE SPELLING, not merely per pitch
// class — a sharp and its enharmonic flat share a key but wear different
// colors (C# is warm, Db is cool). The index is letter*3 + (sign+1): the
// diatonic letter (C=0 .. B=6) times three, offset by the accidental
// (flat 0 / natural 1 / sharp 2). Hues walk the LINE OF FIFTHS in CIELAB,
// so a semitone step lands on the far side of the wheel — chromatic
// neighbours contrast hard — while sharps stay warm, flats stay cool, and
// naturals hold the blue-to-magenta arc. Tuned to read on the white sheet,
// white keys, and black keys; the verdict pure-red/green never appear here.
constexpr int kGuidancePaletteSize = 21;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 139, 135, 0 }, //  0 Cb
    { 3, 141, 190 }, //  1 C   blue
    { 221, 89, 73 }, //  2 C#  warm red
    { 40, 151, 63 }, //  3 Db  cool green
    { 94, 125, 233 }, //  4 D   indigo
    { 177, 121, 5 }, //  5 D#  amber
    { 0, 148, 136 }, //  6 Eb  teal
    { 199, 91, 183 }, //  7 E   magenta
    { 106, 144, 21 }, //  8 E#
    { 173, 123, 0 }, //  9 Fb
    { 9, 144, 171 }, // 10 F   cyan-blue
    { 228, 78, 109 }, // 11 F#  warm pink-red
    { 100, 145, 27 }, // 12 Gb  cool olive-green
    { 0, 137, 218 }, // 13 G   blue
    { 203, 105, 40 }, // 14 G#  orange
    { 0, 150, 108 }, // 15 Ab  green
    { 159, 108, 213 }, // 16 A   violet
    { 144, 134, 1 }, // 17 A#  warm gold
    { 1, 146, 155 }, // 18 Bb  cool cyan
    { 221, 78, 147 }, // 19 B   pink
    { 53, 150, 57 }, // 20 B#
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

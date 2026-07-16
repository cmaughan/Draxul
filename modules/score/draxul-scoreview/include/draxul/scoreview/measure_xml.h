#pragma once

// The minimal MusicXML measure writer (plans/scoreview-stream.md S3): pure
// helpers that fabricate <measure> bodies for drill/exercise bars. Composers
// decide WHAT to drill; this file owns HOW a measure is emitted, so every
// composer shares one testable writer. Future tuplet-fidelity work lands
// here, not in composers.

#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

// <pitch> for a MIDI note, spelled with sharps (C, C#, D, ...).
std::string pitch_xml(int midi);

// A complete <note> element. `duration` is in MusicXML divisions; `chord`
// marks stacked chord members after the first.
std::string note_xml(
    int midi, int duration, const char* type, int voice, int staff, bool chord = false);

// "60+64+67"-style chord keys (PlayerModel::chord_key) back to sorted pitches.
std::vector<int> parse_chord_key(const std::string& key);

// A one-bar chord drill: the grab in the right hand over the bass held for
// the whole bar. Broken form arpeggiates the grab as eighths and lands the
// block chord on the final beat (the easier rung of the ladder); block form
// strikes the grab on every beat. Empty when the spec holds no pitches.
struct ChordDrillSpec
{
    std::vector<int> pitches; // sorted; front() is the bass
    int divisions = 1; // MusicXML divisions in force at the reference bar
    int beats = 4; // quarters in the bar
    bool broken = false;
};
std::string chord_drill_measure_xml(const ChordDrillSpec& spec);

// A one-bar scale fragment through a troubled register, in the given key:
// scale degrees ascend from the tonic nearest below the register's center,
// as eighths when the divisions allow, quarters otherwise.
struct ScaleBarSpec
{
    int tonic_pc = 0; // 0 = C
    bool minor = false;
    int center_pitch = 60; // center of the troubled register
    int divisions = 1;
    int beats = 4;
};
std::string scale_measure_xml(const ScaleBarSpec& spec);

} // namespace scoreview
} // namespace draxul

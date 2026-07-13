#pragma once

// Piece analysis (plans/scoreview-stream.md S1): the upfront understanding
// pass the composer builds on. Pure — input is the judgment axis itself
// (onset qstamps + sounding pitches), so analysis and gameplay can never
// disagree about what the piece contains.
//
// - Key estimate: Krumhansl–Kessler profile correlation over an
//   IOI-weighted pitch-class histogram, notated signature as a prior,
//   windowed over bars to catch modulations.
// - Chord inventory + "nearings": vertical sonorities classified against
//   triad/seventh templates (waltz-style dyads borrow the bar's bass),
//   with successor counts — the piece's harmonic vocabulary AND join table.
// - Motifs: recurring melodic interval n-grams from the top voice.
// - Rhythm figures: per-beat onset patterns on a twelfth-of-a-quarter grid
//   (triplets and dotted figures fall out naturally) — the timing-drill
//   targets whose drift statistics S0 tracks.

#include <optional>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct AnalysisOnset
{
    double qstamp = 0.0;
    std::vector<int> pitches; // sounding MIDI pitches struck at this onset
};

struct PieceProfile
{
    struct KeyEstimate
    {
        int tonic_pc = 0; // 0 = C
        bool minor = false;
        double confidence = 0.0; // correlation margin over the runner-up
    };
    struct KeySection
    {
        double start_q = 0.0;
        double end_q = 0.0;
        KeyEstimate key;
    };
    struct Chord
    {
        std::string name; // e.g. "A min", "E dom7"
        int count = 0;
        // Top successors with counts — the chord's "nearings".
        std::vector<std::pair<std::string, int>> next;
    };
    struct Motif
    {
        std::vector<int> intervals; // semitone steps between melody notes
        int count = 0;
        double first_q = 0.0;
    };
    struct Figure
    {
        std::string signature; // onset offsets in twelfths, e.g. "0,4,8"
        std::string name; // musical name when recognized ("triplet")
        int count = 0;
        std::vector<double> example_beats; // first few beat qstamps
    };

    KeyEstimate global_key;
    std::vector<KeySection> key_sections; // only when the key moves
    std::vector<Chord> chords; // by count, descending
    std::vector<Motif> motifs; // by (count, length), descending
    std::vector<Figure> figures; // by count, descending

    std::string serialize() const; // pretty JSON (the debug/dump format)
};

std::string key_name(int tonic_pc, bool minor);

PieceProfile analyze_piece(const std::vector<AnalysisOnset>& onsets, double quarters_per_bar,
    std::optional<int> notated_fifths);

} // namespace scoreview
} // namespace draxul

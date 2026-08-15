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
// - Motifs: recurring melodic patterns from the top voice, mined with a
//   bounded-depth suffix trie whose edges carry (interval, step rhythm):
//   a motif is a maximal well-supported path — deep enough to mean
//   something (4..8 notes), recurring enough to matter, ending where its
//   continuations diverge. Melody sequences break at rests, so a motif
//   never straddles a breath.
// - Rhythm figures: per-beat onset patterns on a twelfth-of-a-quarter grid
//   (triplets and dotted figures fall out naturally) — the timing-drill
//   targets whose drift statistics S0 tracks.
// - Structure (C0, plans/scoreview-composer.md): phrases and sections, from
//   cadences, rests, motif restarts and key changes. This is the practice
//   chunk — the evidence is that expert practice starts and stops at
//   structural boundaries and that the formal hierarchy IS the retrieval
//   scheme, so the composer must never chunk on fixed-length windows.

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
    // Per-pitch durations in quarters, parallel to `pitches` (empty or 0 =
    // unknown). Durations drive the sustain-aware skyline: a long melody
    // note SHADOWS accompaniment struck beneath it while it rings.
    std::vector<double> durations;
};

// One melody note chosen by the sustain-aware skyline. `onset`/`voice` index
// back into the AnalysisOnset list (voice indexes that onset's pitches), so
// callers can recover ids or durations for the chosen note.
struct MelodyNote
{
    double qstamp = 0.0;
    int pitch = -1;
    size_t onset = 0;
    size_t voice = 0;
};

// The melody line: highest sounding pitch, where a ringing note shadows
// lower onsets struck under it (per-onset "top pitch" would hear the
// accompaniment as melody in every gap — the classic skyline failure).
// An onset joins the melody only when the previous melody note has ended or
// the newcomer is higher (a real voice crossing). Unknown durations shadow
// nothing. This is THE melody definition: motif mining, phrase-shape
// matching and the overlay's motif coloring must all use it, or occurrence
// positions and colored notes disagree.
std::vector<MelodyNote> skyline_melody(const std::vector<AnalysisOnset>& onsets);

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
        // The step rhythm, parallel to `intervals`: time to the next melody
        // note in twelfths of a quarter. A motif is a JOINT pitch+rhythm
        // pattern — the same contour in straight quarters and in triplets is
        // two motifs.
        std::vector<int> ioi_twelfths;
        int count = 0;
        double first_q = 0.0;
        std::vector<double> occurrences_q; // every start qstamp, ascending
    };
    struct Figure
    {
        std::string signature; // onset offsets in twelfths, e.g. "0,4,8"
        std::string name; // musical name when recognized ("triplet")
        int count = 0;
        std::vector<double> example_beats; // first few beat qstamps
    };
    // A phrase: the unit the composer serves as one practice chunk.
    struct Phrase
    {
        int start_bar = 0; // inclusive
        int end_bar = 0; // exclusive
        double start_q = 0.0;
        double end_q = 0.0;
        // Evidence behind this phrase's START boundary (0..1). The opening
        // phrase has none — nothing precedes it to cadence.
        double confidence = 0.0;
        // Restatement: the earliest phrase this one repeats (-1 = new
        // material). Matching is on the melody's interval + rhythm shape, so
        // a transposed restatement still matches; `transposed` distinguishes
        // it from a literal repeat. A repeated phrase is not NEW learning —
        // the piece's effective size is smaller than its bar count, and
        // mastery evidence may transfer between repeats.
        int repeat_of = -1;
        bool transposed = false;
    };
    // A section: a coarse grouping of phrases. Only split where there is real
    // evidence (the key moves); a piece that never modulates is one section,
    // because inventing section structure is worse than admitting we can't
    // see it.
    struct Section
    {
        int start_bar = 0; // inclusive
        int end_bar = 0; // exclusive
        double start_q = 0.0;
        double end_q = 0.0;
        std::vector<int> phrase_ids; // indices into `phrases`
    };
    // Where a bar sits in the structure. Serial position is the load-bearing
    // field: recall collapses from a phrase's opening bar to its interior, so
    // the composer weights tails over heads.
    struct BarPosition
    {
        int section_id = -1;
        int phrase_id = -1;
        int serial_in_phrase = 0; // 0 = the phrase's opening bar
        int phrase_length = 1; // bars in the phrase
        bool phrase_open = false; // the privileged retrieval cue (anchor)
        bool section_open = false;
    };

    KeyEstimate global_key;
    std::vector<KeySection> key_sections; // only when the key moves
    std::vector<Chord> chords; // by count, descending
    std::vector<Motif> motifs; // by (count, length), descending
    std::vector<Figure> figures; // by count, descending
    std::vector<Phrase> phrases; // in bar order, covering the piece
    std::vector<Section> sections; // in bar order, covering the piece
    std::vector<BarPosition> bar_positions; // indexed by bar
    // Mean evidence behind the interior boundaries (0..1). Deliberately
    // excluded: the hypermetric prior, and any boundary forced by the maximum
    // phrase length (which is an admission that we had to chunk somewhere, not
    // a detection). A piece whose "phrasing" is only guesswork must therefore
    // report low confidence, so the composer falls back to fixed slices rather
    // than trusting invented structure.
    double structure_confidence = 0.0;

    // The phrase covering `bar`, or nullptr when unanalyzed/out of range.
    const Phrase* phrase_at_bar(int bar) const;
    // Position within the phrase, 0 at the opening bar .. 1 at the last bar
    // (0 when unknown or the phrase is a single bar).
    double bar_tail_fraction(int bar) const;

    std::string serialize() const; // pretty JSON (the debug/dump format)
};

std::string key_name(int tonic_pc, bool minor);
// Scientific-pitch name for a MIDI note (e.g. 60 -> "C4"), sharp spelling;
// "?" for a negative pitch. The one pitch-class table outside measure_xml
// (whose step+alter encoding is a different thing).
std::string note_name(int midi);

PieceProfile analyze_piece(const std::vector<AnalysisOnset>& onsets, double quarters_per_bar,
    std::optional<int> notated_fifths);

} // namespace scoreview
} // namespace draxul

#pragma once

// The judge→memory outcome vocabulary (plans/scoreview-stream.md S0): the
// per-note and per-chord learning signals the Roll-mode judge emits and the
// player model aggregates. Lives below both so the model (and the composer
// stack above it) never includes the transport layer.

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

enum class NoteVerdict : uint8_t
{
    Pending,
    Correct,
    Missed,
};

// Per-note learning outcome (Roll mode): everything the player model
// aggregates. Hits carry the signed timing delta in beats (negative = early)
// and a center-weighted quality; misses and strays carry zeros.
// Auto-satisfied notes (ties) emit nothing — they are not learning signals.
struct NoteOutcome
{
    std::string id; // element id; empty for stray notes
    double onset_q = 0.0; // source qstamp (strays: transport position)
    int pitch = -1;
    NoteVerdict verdict = NoteVerdict::Missed;
    bool stray = false; // matched no onset (wrong pitch / bad timing)
    double delta_q = 0.0; // hit: position - onset, in beats
    double quality = 0.0; // hit: 1 at center, kRollEdgeQuality at edge
};

// Chord-level outcome, emitted when a multi-note onset's window closes.
struct ChordOutcome
{
    double onset_q = 0.0;
    std::vector<int> pitches; // required pitches, sorted
    enum class Result : uint8_t
    {
        Clean, // all correct, struck together
        Split, // all correct, but spread beyond kChordSplitQ
        Miss, // at least one note missed
    } result = Result::Miss;
};

} // namespace scoreview
} // namespace draxul

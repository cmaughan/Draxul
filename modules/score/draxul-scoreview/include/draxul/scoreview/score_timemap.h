#pragma once

#include <draxul/scoreview/piece_analysis.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{
namespace scoreview
{

// One playback-order event from Verovio's timemap: notes starting and/or
// ending at a quarter-note timestamp. qstamp is the canonical transport axis
// for the conveyor (plans/scoreview-conveyor.md); Verovio's millisecond
// tstamps are ignored — the runner owns its own tempo.
struct TimemapEntry
{
    double qstamp = 0.0;
    std::vector<std::string> note_on; // element ids starting here
    std::vector<std::string> note_off; // element ids ending here
};

struct Timemap
{
    std::vector<TimemapEntry> entries; // ascending qstamp
    // First tempo marking encountered (quarter notes per minute); 0 when the
    // piece carries none.
    double tempo_qpm = 0.0;
    // Last entry's qstamp — the piece's total length in quarter notes.
    double duration_q = 0.0;

    bool empty() const
    {
        return entries.empty();
    }
};

// Parses Verovio's RenderToTimemap() JSON. Tolerant of unknown keys and
// entries without ons/offs; entries missing a qstamp are skipped. Returns
// std::nullopt and fills `error` for structurally invalid JSON.
std::optional<Timemap> parse_timemap(std::string_view json_text, std::string& error);

// The analysis input from a timemap: one AnalysisOnset per entry with sounding
// pitches AND durations (note_off minus note_on — what the sustain-aware
// skyline needs to shadow accompaniment under ringing melody notes).
// `midi_pitch` resolves an element id to its sounding pitch (< 0 = skip).
// `ids_out` (optional) receives the element ids parallel to each onset's
// pitches — the overlay maps melody choices back to engraved noteheads.
std::vector<AnalysisOnset> analysis_onsets_from_timemap(const Timemap& timemap,
    const std::function<int(const std::string&)>& midi_pitch,
    std::vector<std::vector<std::string>>* ids_out = nullptr);

} // namespace scoreview
} // namespace draxul

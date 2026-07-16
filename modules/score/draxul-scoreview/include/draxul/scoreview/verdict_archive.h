#pragma once

// Verdicts already earned on the stream axis (plans/scoreview-stream.md S2):
// the rolling window re-engraves at bar boundaries and replays these onto
// the fresh strip so judged notes keep their paint. Keyed by quantized
// stream position + pitch — the quantization lives HERE and nowhere else, so
// write and replay can never disagree about rounding.
//
// Committed history only: callers record outcomes as their judging windows
// close, never ahead of the playhead, and slot geometry behind the engrave
// frontier never changes (StreamProgram's invariant). That is exactly why a
// future rewriting composer may re-plan the open future without stranding
// this archive.

#include <draxul/scoreview/note_outcomes.h>

#include <cstddef>
#include <functional>
#include <map>
#include <utility>

namespace draxul
{
namespace scoreview
{

class VerdictArchive
{
public:
    void clear()
    {
        entries_.clear();
    }
    bool empty() const
    {
        return entries_.empty();
    }
    std::size_t size() const
    {
        return entries_.size();
    }

    // Records a judged (non-stray) verdict at its stream position. A repeat
    // at the same quantized position and pitch overwrites — the latest
    // judgment is the one the repaint shows.
    void record(double stream_q, int pitch, NoteVerdict verdict);

    // Replays every entry inside [window_start_q, window_end_q] (stream
    // axis; a small tolerance admits the window's exact head) as
    // (local_q, pitch, verdict), local to window_start_q — the currency
    // FlowController::preset_verdict expects.
    void replay(double window_start_q, double window_end_q,
        const std::function<void(double local_q, int pitch, NoteVerdict verdict)>& apply) const;

private:
    // The ONE quantization site: thousandths of a quarter note.
    static long long quantize(double stream_q);

    std::map<std::pair<long long, int>, NoteVerdict> entries_;
};

} // namespace scoreview
} // namespace draxul

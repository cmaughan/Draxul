#pragma once

// The stream program (plans/scoreview-stream.md S3): the slot-by-slot plan a
// composer produces and the host consumes — what each slot plays, where it
// sits on the stream axis, and how a stream position maps back to the SOURCE
// axis (provenance). The host owns the program; a composer extends it via
// ensure(program, slots), and program + composer state reset together
// (ScoreHost::reset_stream_plan).
//
// The program is append-only behind the PLAYHEAD: geometry for slots already
// played/judged never changes (the stream-q-keyed verdict archive and the
// window carry depend on it). Ahead of the playhead the program may be
// REWRITTEN by splicing (insert): the urgent-fix path inserts a correction
// just past the playhead and the window re-engraves around it — the sheet
// ahead of the player updates while the current window keeps playing.

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct StreamBarPlan
{
    enum class Kind : uint8_t
    {
        Piece, // the frontier bar, verbatim
        Review, // a weak earlier bar, revisited (spaced repetition)
        Drill, // a fabricated exercise bar
    };
    Kind kind = Kind::Piece;
    // Piece/Review: the bar to play. Drill: the reference bar whose
    // attribute state and provenance context the drill inherits.
    int source_bar = -1;
    // The source bar's start on the SOURCE axis (SourceSlicer::bar_start_q),
    // filled by the composer at plan time so provenance needs no slicer.
    // Drill slots keep the reference bar's start but normally map to the
    // drill sentinel instead (see StreamProgram::source_at). A drill that is
    // just a source-preserving simplification, such as hands separate, opts
    // back into source training.
    double source_start_q = 0.0;
    bool drill_trains_source = false;
    std::string drill_xml; // Drill only: the fabricated <measure>
    std::string reason; // human-readable, for logs and debugging
};

class StreamProgram
{
public:
    void clear();
    int size() const
    {
        return static_cast<int>(program_.size());
    }
    bool empty() const
    {
        return program_.empty();
    }
    const StreamBarPlan& plan(int slot) const
    {
        return program_[static_cast<size_t>(slot)];
    }
    // Appends a slot spanning `quarters` on the stream axis.
    void append(StreamBarPlan plan, double quarters);
    // Splices a slot in at `at_slot`, shifting everything after it later on
    // the stream axis. The REWRITE primitive: callers must only splice
    // beyond the played/committed region (the playhead plus a guard bar) —
    // the stream-q-keyed verdict archive and the window carry depend on
    // committed geometry never moving.
    void insert(int at_slot, StreamBarPlan plan, double quarters);

    // Stream-axis geometry over the program (slot 0 starts at 0).
    double slot_start_q(int slot) const;
    double slot_quarters(int slot) const;
    int slot_at(double stream_q) const;

    // Provenance: where a stream position lives on the SOURCE axis. Ordinary
    // drill slots have no source location (their outcomes train pitch/chord
    // stats only, never bar/onset mastery); Piece/Review and source-training
    // drill positions map bar-relative onto the source bar.
    struct SourceRef
    {
        bool drill = false;
        int source_bar = -1;
        double source_q = 0.0; // valid when !drill
    };
    SourceRef source_at(double stream_q) const;

private:
    std::vector<StreamBarPlan> program_;
    std::vector<double> slot_start_q_{ 0.0 }; // size = program_.size() + 1
};

class SourceSlicer;

// The plan-building vocabulary a composer writes in (kanban 22): compute a
// slot's stream-axis span and source provenance from the slicer so an
// implementation never hand-assembles a StreamBarPlan or repeats the
// bar_start_q / bar_quarters bookkeeping. Every planned slot spans
// `slicer.bar_quarters(bar)` on the stream axis and carries
// `slicer.bar_start_q(bar)` as its source anchor.
//
// append_source_bar plans a Piece/Review slot playing source `bar` verbatim;
// append_fabricated plans a Drill slot from a fabricated <measure> in the
// attribute context of `reference_bar` (`trains_source` opts a
// source-preserving drill, such as hands separate, back into source training).
// The insert_* variants are the rewrite primitive (plan_urgent): the same slot
// spliced in at `at_slot` instead of appended.
void append_source_bar(StreamProgram& program, const SourceSlicer& slicer,
    StreamBarPlan::Kind kind, int bar, std::string reason);
void append_fabricated(StreamProgram& program, const SourceSlicer& slicer, int reference_bar,
    std::string xml, std::string reason, bool trains_source = false);
void insert_source_bar(StreamProgram& program, const SourceSlicer& slicer, int at_slot,
    StreamBarPlan::Kind kind, int bar, std::string reason);
void insert_fabricated(StreamProgram& program, const SourceSlicer& slicer, int at_slot,
    int reference_bar, std::string xml, std::string reason, bool trains_source = false);

} // namespace scoreview
} // namespace draxul

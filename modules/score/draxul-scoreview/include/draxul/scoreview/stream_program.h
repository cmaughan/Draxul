#pragma once

// The stream program (plans/scoreview-stream.md S3): the slot-by-slot plan a
// composer produces and the host consumes — what each slot plays, where it
// sits on the stream axis, and how a stream position maps back to the SOURCE
// axis (provenance). The host owns the program; a composer extends it via
// ensure(program, slots), and program + composer state reset together
// (ScoreHost::reset_stream_plan).
//
// The program is append-only behind the engrave frontier: geometry for slots
// already engraved/judged never changes (the stream-q-keyed verdict archive
// and the window carry depend on it). A future rewriting composer may re-plan
// only the open future beyond that frontier.

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
    // Drill slots keep the reference bar's start but map to the drill
    // sentinel instead (see StreamProgram::source_at).
    double source_start_q = 0.0;
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

    // Stream-axis geometry over the program (slot 0 starts at 0).
    double slot_start_q(int slot) const;
    double slot_quarters(int slot) const;
    int slot_at(double stream_q) const;

    // Provenance: where a stream position lives on the SOURCE axis. Drill
    // slots have no source location (their outcomes train pitch/chord stats
    // only, never bar/onset mastery); Piece/Review positions map
    // bar-relative onto the source bar.
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

} // namespace scoreview
} // namespace draxul

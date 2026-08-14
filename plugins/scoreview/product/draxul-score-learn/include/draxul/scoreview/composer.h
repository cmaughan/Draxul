#pragma once

// The composer seam (plans/scoreview-stream.md S3): what any stream composer
// may observe (source material, player model, piece profile — nothing else)
// and what it produces (a StreamProgram of slots). Implementations decide the
// pedagogy: StreamComposer modifies and interleaves the piece; future
// composers may rewrite it. The host owns the program and the composer
// lifecycle; program and composer state always reset together.

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/stream_program.h>

namespace draxul
{
namespace scoreview
{

class IComposer
{
public:
    virtual ~IComposer() = default;

    // Short stable identifier for logs and the inspector.
    virtual const char* name() const = 0;
    // Can this composer drive this material? Source compatibility is a
    // composer capability, not a host rule — e.g. the adaptive stream
    // fabricates grand-staff drills, so it requires a single-part source.
    virtual bool supports(const SourceSlicer& slicer) const = 0;
    // The composer observes these read-only; they must outlive its use.
    virtual void configure(
        const SourceSlicer* slicer, const PlayerModel* model, const PieceProfile* profile)
        = 0;
    virtual bool ready() const = 0;
    // Clears policy state only; the owner clears the paired program in the
    // same breath (slot-indexed cooldowns must never diverge from it).
    virtual void reset() = 0;
    // Extends `program` up to `slots` entries (may stop early); returns the
    // planned count. Always handed the same program since the last reset().
    virtual int ensure(StreamProgram& program, int slots) = 0;
    // Pedagogy toggles a composer may honor or ignore: fabricated chord
    // drills and scale fragments. Both default OFF while their value is
    // evaluated against real play — reviews, seams, fixes and the
    // simplification ladder are unaffected.
    virtual void set_drills_enabled(bool enabled)
    {
        (void)enabled;
    }
    virtual void set_scales_enabled(bool enabled)
    {
        (void)enabled;
    }
    // The REWRITE hook: splice urgent correction slots in at `at_slot`
    // (already guarded past the playhead by the caller), returning how many
    // were inserted. The composer must keep its own slot-indexed bookkeeping
    // consistent with the shift. Default: no urgent planning.
    virtual int plan_urgent(StreamProgram& program, int at_slot)
    {
        (void)program;
        (void)at_slot;
        return 0;
    }
    virtual bool finished() const = 0;
};

} // namespace scoreview
} // namespace draxul

// The composer-agnostic contract suite (kanban 22): the seam invariants ANY
// IComposer must satisfy, independent of its pedagogy. scoreview_composer_tests
// covers what the adaptive stream *teaches*; this file covers what the seam
// *guarantees* — append-only geometry behind the playhead, provenance that
// round-trips, splices that leave the committed region untouched and re-plan
// coherently, reset() that re-plans from scratch, and a sticky finished().
//
// A second composer registers by adding a factory to registered_composers()
// and gets the whole contract for free. The final case is adaptive-stream
// specific: it proves the splice keeps the slot-indexed cooldowns consistent —
// the failure a missed SlotCooldowns shift would introduce.

#include <catch2/catch_all.hpp>

#include "support/scoreview_engrave_helpers.h"

#include <draxul/scoreview/composer.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/stream_composer.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace draxul::scoreview;

namespace
{

// A composer under test: a display name and a factory. Register a second
// composer here and every generic contract case below runs against it too.
struct ComposerCase
{
    std::string name;
    std::function<std::unique_ptr<IComposer>()> make;
};

std::vector<ComposerCase> registered_composers()
{
    std::vector<ComposerCase> cases;
    cases.push_back({ "adaptive-stream", [] { return std::make_unique<StreamComposer>(); } });
    return cases;
}

SourceSlicer& grieg()
{
    static SourceSlicer slicer;
    static bool loaded = false;
    if (!loaded)
    {
        std::string error;
        loaded = slicer.load(read_grieg_xml(), error);
    }
    REQUIRE(loaded);
    return slicer;
}

// A handful of encountered-but-weak bars, so any composer that reviews weak
// material has something to schedule. Mirrors add_weak_bar in the pedagogy
// suite (three missed onsets, no explicit pass close).
void make_weak(PlayerModel& model, double qpb)
{
    model.set_piece("contract", 120.0, qpb);
    for (const int bar : { 1, 3, 5, 7, 9 })
    {
        for (int beat = 0; beat < 3; ++beat)
        {
            NoteOutcome outcome;
            outcome.onset_q = bar * qpb + beat;
            outcome.pitch = 60;
            outcome.verdict = NoteVerdict::Missed;
            outcome.quality = 0.0;
            model.apply(outcome);
        }
    }
}

// Every bar swept clean three consecutive times, so a convergence composer
// earns its finish. Mirrors the mastered-model setup in the pedagogy suite.
void make_mastered(PlayerModel& model, const SourceSlicer& slicer, double qpb)
{
    model.set_piece("contract", 120.0, qpb);
    for (int sweep = 0; sweep < 3; ++sweep)
    {
        for (int bar = 0; bar < slicer.bar_count(); ++bar)
        {
            for (int beat = 0; beat < 3; ++beat)
            {
                NoteOutcome outcome;
                outcome.onset_q = bar * qpb + beat;
                outcome.pitch = 60;
                outcome.verdict = NoteVerdict::Correct;
                outcome.quality = 1.0;
                model.apply(outcome);
            }
        }
    }
    model.close_open_pass();
}

// A live fumble on `bar`: three onsets with the middle one missed, then a note
// in the next bar to close the pass — exactly how the judge feeds the model.
void fumble_bar(PlayerModel& model, int bar, double qpb)
{
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = bar * qpb + beat;
        outcome.pitch = 60;
        outcome.verdict = beat == 1 ? NoteVerdict::Missed : NoteVerdict::Correct;
        outcome.quality = beat == 1 ? 0.0 : 1.0;
        model.apply(outcome);
    }
    NoteOutcome closer;
    closer.onset_q = (bar + 1) * qpb;
    closer.pitch = 60;
    closer.verdict = NoteVerdict::Correct;
    closer.quality = 1.0;
    model.apply(closer);
}

// The provenance contract at one stream position: source_at agrees with the
// slot's own plan, and slot_at inverts the geometry.
void check_provenance_at_slot(const StreamProgram& program, int slot)
{
    const StreamBarPlan& plan = program.plan(slot);
    const double mid = program.slot_start_q(slot) + program.slot_quarters(slot) * 0.5;
    CHECK(program.slot_at(mid) == slot);
    const StreamProgram::SourceRef ref = program.source_at(mid);
    const bool expect_drill
        = plan.kind == StreamBarPlan::Kind::Drill && !plan.drill_trains_source;
    CHECK(ref.drill == expect_drill);
    if (!ref.drill)
    {
        CHECK(ref.source_bar == plan.source_bar);
        CHECK(ref.source_q == Catch::Approx(plan.source_start_q + program.slot_quarters(slot) * 0.5));
    }
}

// Stream geometry is monotonic: each slot has positive span and starts where
// the previous ended.
void check_geometry_monotonic(const StreamProgram& program)
{
    for (int slot = 0; slot < program.size(); ++slot)
    {
        CHECK(program.slot_quarters(slot) > 0.0);
        CHECK(program.slot_start_q(slot + 1)
            == Catch::Approx(program.slot_start_q(slot) + program.slot_quarters(slot)));
    }
}

} // namespace

TEST_CASE("composer contract: geometry never changes at or behind a planned slot",
    "[scoreview][composer][contract]")
{
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    for (const ComposerCase& c : registered_composers())
    {
        INFO("composer: " << c.name);
        PlayerModel model;
        make_weak(model, qpb);
        PieceProfile profile;
        auto composer = c.make();
        composer->configure(&slicer, &model, &profile);

        StreamProgram program;
        composer->ensure(program, 24);
        REQUIRE(program.size() >= 20);

        struct Slot
        {
            StreamBarPlan::Kind kind;
            int source_bar;
            double start_q;
        };
        std::vector<Slot> before;
        for (int slot = 0; slot < program.size(); ++slot)
            before.push_back(
                { program.plan(slot).kind, program.plan(slot).source_bar, program.slot_start_q(slot) });

        // Extending the SAME program must never rewrite a committed slot.
        composer->ensure(program, program.size() + 16);
        for (int slot = 0; slot < static_cast<int>(before.size()); ++slot)
        {
            CHECK(program.plan(slot).kind == before[static_cast<size_t>(slot)].kind);
            CHECK(program.plan(slot).source_bar == before[static_cast<size_t>(slot)].source_bar);
            CHECK(program.slot_start_q(slot)
                == Catch::Approx(before[static_cast<size_t>(slot)].start_q));
        }
        check_geometry_monotonic(program);
    }
}

TEST_CASE("composer contract: source_at provenance round-trips for every planned slot",
    "[scoreview][composer][contract]")
{
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    for (const ComposerCase& c : registered_composers())
    {
        INFO("composer: " << c.name);
        PlayerModel model;
        make_weak(model, qpb);
        PieceProfile profile;
        auto composer = c.make();
        composer->configure(&slicer, &model, &profile);

        StreamProgram program;
        composer->ensure(program, 32);
        REQUIRE(program.size() > 0);
        for (int slot = 0; slot < program.size(); ++slot)
            check_provenance_at_slot(program, slot);
    }
}

TEST_CASE("composer contract: plan_urgent leaves the pre-splice region intact and re-plans coherently",
    "[scoreview][composer][contract]")
{
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    for (const ComposerCase& c : registered_composers())
    {
        INFO("composer: " << c.name);
        PlayerModel model;
        make_weak(model, qpb);
        PieceProfile profile;
        auto composer = c.make();
        composer->configure(&slicer, &model, &profile);

        StreamProgram program;
        composer->ensure(program, 16);
        const int before_size = program.size();
        REQUIRE(before_size >= 12);

        // A live fumble the splice can claim.
        fumble_bar(model, 2, qpb);

        struct Slot
        {
            StreamBarPlan::Kind kind;
            int source_bar;
            double start_q;
        };
        std::vector<Slot> snapshot;
        for (int slot = 0; slot < before_size; ++slot)
            snapshot.push_back(
                { program.plan(slot).kind, program.plan(slot).source_bar, program.slot_start_q(slot) });

        const int at_slot = 6; // past the playhead, well inside the committed program
        const int inserted = composer->plan_urgent(program, at_slot);
        if (inserted == 0)
        {
            // A composer without a rewrite hook (the IComposer default) simply
            // skips this contract — nothing was spliced, nothing to check.
            SUCCEED("composer does not implement plan_urgent");
            continue;
        }
        REQUIRE(program.size() == before_size + inserted);

        // The spliced span, summed over the inserted slots.
        double inserted_q = 0.0;
        for (int slot = at_slot; slot < at_slot + inserted; ++slot)
            inserted_q += program.slot_quarters(slot);

        // Everything BEFORE the splice is byte-for-byte where it was.
        for (int slot = 0; slot < at_slot; ++slot)
        {
            CHECK(program.plan(slot).kind == snapshot[static_cast<size_t>(slot)].kind);
            CHECK(program.plan(slot).source_bar == snapshot[static_cast<size_t>(slot)].source_bar);
            CHECK(program.slot_start_q(slot)
                == Catch::Approx(snapshot[static_cast<size_t>(slot)].start_q));
        }
        // Everything at/after the splice is the old plan, shifted later by the
        // spliced span — geometry ahead of the player moved, provenance intact.
        for (int old_slot = at_slot; old_slot < before_size; ++old_slot)
        {
            const int now = old_slot + inserted;
            CHECK(program.plan(now).kind == snapshot[static_cast<size_t>(old_slot)].kind);
            CHECK(program.plan(now).source_bar == snapshot[static_cast<size_t>(old_slot)].source_bar);
            CHECK(program.slot_start_q(now)
                == Catch::Approx(snapshot[static_cast<size_t>(old_slot)].start_q + inserted_q));
        }

        // ...and the composer keeps planning coherently on top of the splice.
        composer->ensure(program, program.size() + 12);
        check_geometry_monotonic(program);
        for (int slot = 0; slot < program.size(); ++slot)
            check_provenance_at_slot(program, slot);
    }
}

TEST_CASE("composer contract: reset() plus a program clear leaves a re-plannable state",
    "[scoreview][composer][contract]")
{
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    for (const ComposerCase& c : registered_composers())
    {
        INFO("composer: " << c.name);
        PlayerModel model;
        make_weak(model, qpb);
        PieceProfile profile;
        auto composer = c.make();
        composer->configure(&slicer, &model, &profile);

        StreamProgram program;
        composer->ensure(program, 20);
        REQUIRE(program.size() >= 20);

        // The owner clears policy state and the paired program together.
        composer->reset();
        program.clear();
        REQUIRE(program.empty());

        composer->ensure(program, 20);
        CHECK(program.size() >= 20); // re-plans from scratch
        CHECK(program.slot_start_q(0) == Catch::Approx(0.0));
        check_geometry_monotonic(program);
    }
}

TEST_CASE("composer contract: finished() is sticky", "[scoreview][composer][contract]")
{
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    for (const ComposerCase& c : registered_composers())
    {
        INFO("composer: " << c.name);
        PlayerModel model;
        make_mastered(model, slicer, qpb);
        PieceProfile profile;
        auto composer = c.make();
        composer->configure(&slicer, &model, &profile);

        StreamProgram program;
        composer->ensure(program, 4 * slicer.bar_count());
        if (!composer->finished())
        {
            // A composer that never finishes (an endless one) is a valid seam
            // citizen; the stickiness contract only binds those that do.
            SUCCEED("composer does not finish on a mastered piece");
            continue;
        }
        const int settled_size = program.size();
        // Once finished, more ensure() calls neither un-finish nor grow it.
        composer->ensure(program, program.size() + 32);
        CHECK(composer->finished());
        CHECK(program.size() == settled_size);
    }
}

TEST_CASE("adaptive-stream contract: a splice keeps the slot cooldowns consistent",
    "[scoreview][composer][contract]")
{
    // The splice-consistency case: after a rewrite shifts the program, a review
    // must still honor its cooldown against its SHIFTED slot. A missed
    // SlotCooldowns shift would leave a stale slot behind and let the same bar
    // recur one slot too early — this scan across the whole program catches it.
    SourceSlicer& slicer = grieg();
    const double qpb = slicer.bar_quarters(0);
    PlayerModel model;
    make_weak(model, qpb);
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);

    StreamProgram program;
    composer.ensure(program, 40);
    // Splice a live fix a few slots past the playhead, then keep planning.
    fumble_bar(model, 5, qpb);
    composer.plan_urgent(program, 4);
    composer.ensure(program, program.size() + 40);

    // try_review slots carry "review bar N" (distinct from seams/opening); no
    // bar may recur within kDrillCooldownSlots slots of its last review.
    std::map<int, int> last_review_slot;
    for (int slot = 0; slot < program.size(); ++slot)
    {
        const StreamBarPlan& plan = program.plan(slot);
        if (plan.reason.rfind("review bar ", 0) != 0)
            continue;
        const auto last = last_review_slot.find(plan.source_bar);
        if (last != last_review_slot.end())
        {
            INFO("review of bar " << (plan.source_bar + 1) << " recurred at slot " << slot
                                  << " (last at " << last->second << ")");
            CHECK(slot - last->second >= StreamComposer::kDrillCooldownSlots);
        }
        last_review_slot[plan.source_bar] = slot;
    }
    // The splice itself must have round-tripped the whole program.
    check_geometry_monotonic(program);
}

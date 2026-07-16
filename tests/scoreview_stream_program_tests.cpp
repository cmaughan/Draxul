// Stream program (plans/scoreview-stream.md S3): slot geometry and the
// stream→source provenance mapping the host feeds outcomes and the guidance
// keyboard through.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/stream_program.h>

using namespace draxul::scoreview;

namespace
{

StreamBarPlan piece_bar(int bar, double source_start_q)
{
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Piece;
    plan.source_bar = bar;
    plan.source_start_q = source_start_q;
    return plan;
}

} // namespace

TEST_CASE("an empty program clamps its geometry to zero", "[scoreview][program]")
{
    StreamProgram program;
    CHECK(program.size() == 0);
    CHECK(program.empty());
    CHECK(program.slot_start_q(0) == 0.0);
    CHECK(program.slot_start_q(5) == 0.0);
    CHECK(program.slot_at(2.0) == 0);
    const auto ref = program.source_at(2.0);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == -1);
}

TEST_CASE("slot geometry accumulates appended quarters", "[scoreview][program]")
{
    StreamProgram program;
    program.append(piece_bar(0, 0.0), 3.0);
    program.append(piece_bar(1, 3.0), 3.0);
    program.append(piece_bar(2, 6.0), 4.0); // meter change
    CHECK(program.size() == 3);
    CHECK(program.slot_start_q(0) == 0.0);
    CHECK(program.slot_start_q(1) == 3.0);
    CHECK(program.slot_start_q(2) == 6.0);
    CHECK(program.slot_start_q(3) == 10.0);
    CHECK(program.slot_quarters(2) == 4.0);
    CHECK(program.slot_at(0.0) == 0);
    CHECK(program.slot_at(3.0) == 1);
    CHECK(program.slot_at(9.9) == 2);
    CHECK(program.slot_at(50.0) == 2); // clamped to the last slot
    program.clear();
    CHECK(program.size() == 0);
    CHECK(program.slot_start_q(1) == 0.0);
}

TEST_CASE("provenance maps piece, review, and drill slots to the source axis",
    "[scoreview][program]")
{
    // Program: piece bar 0, a review of bar 7, a drill (reference bar 1),
    // then piece bar 1 — all 3/4 bars.
    StreamProgram program;
    program.append(piece_bar(0, 0.0), 3.0);
    StreamBarPlan review;
    review.kind = StreamBarPlan::Kind::Review;
    review.source_bar = 7;
    review.source_start_q = 21.0;
    program.append(review, 3.0);
    StreamBarPlan drill;
    drill.kind = StreamBarPlan::Kind::Drill;
    drill.source_bar = 1;
    drill.source_start_q = 3.0;
    drill.drill_xml = "<measure/>";
    program.append(drill, 3.0);
    program.append(piece_bar(1, 3.0), 3.0);

    // Piece slot: bar-relative offset onto the source bar.
    auto ref = program.source_at(1.5);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == 0);
    CHECK(ref.source_q == Catch::Approx(1.5));

    // Review slot: stream q 3..6 trains bar 7's onsets at source 21..24.
    ref = program.source_at(4.0);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == 7);
    CHECK(ref.source_q == Catch::Approx(22.0));

    // Drill slot: no source location — outcomes go to the drill sentinel.
    ref = program.source_at(7.0);
    CHECK(ref.drill);
    CHECK(ref.source_bar == 1);

    // The piece resumes after the specials.
    ref = program.source_at(9.5);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == 1);
    CHECK(ref.source_q == Catch::Approx(3.5));
}

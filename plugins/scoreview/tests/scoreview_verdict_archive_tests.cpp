// Verdict archive (plans/scoreview-stream.md S2): the stream-axis record of
// judged notes the rolling window replays after every re-engrave. The
// quantization lives inside the class — these tests pin the write/replay
// round trip so the two sides can never disagree about rounding.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/verdict_archive.h>

#include <vector>

using namespace draxul::scoreview;

namespace
{

struct Replayed
{
    double local_q = 0.0;
    int pitch = -1;
    NoteVerdict verdict = NoteVerdict::Pending;
};

std::vector<Replayed> replay_all(const VerdictArchive& archive, double start_q, double end_q)
{
    std::vector<Replayed> out;
    archive.replay(start_q, end_q, [&out](double local_q, int pitch, NoteVerdict verdict) {
        out.push_back({ local_q, pitch, verdict });
    });
    return out;
}

} // namespace

TEST_CASE("record and replay round-trip on the stream axis", "[scoreview][verdict-archive]")
{
    VerdictArchive archive;
    CHECK(archive.empty());
    archive.record(12.3456, 60, NoteVerdict::Correct);
    archive.record(15.0, 64, NoteVerdict::Missed);
    CHECK(archive.size() == 2);

    // A window starting at stream q 9: entries come back window-local.
    const auto replayed = replay_all(archive, 9.0, 30.0);
    REQUIRE(replayed.size() == 2);
    CHECK(replayed[0].local_q == Catch::Approx(3.346).margin(0.001));
    CHECK(replayed[0].pitch == 60);
    CHECK(replayed[0].verdict == NoteVerdict::Correct);
    CHECK(replayed[1].local_q == Catch::Approx(6.0).margin(0.001));
    CHECK(replayed[1].verdict == NoteVerdict::Missed);
}

TEST_CASE("replay filters to the window, tolerating its exact head",
    "[scoreview][verdict-archive]")
{
    VerdictArchive archive;
    archive.record(8.9, 60, NoteVerdict::Correct); // behind the window
    archive.record(9.0, 62, NoteVerdict::Correct); // exactly at the head
    archive.record(20.0, 64, NoteVerdict::Correct); // inside
    archive.record(30.0, 65, NoteVerdict::Correct); // exactly at the tail
    archive.record(30.001, 67, NoteVerdict::Correct); // beyond

    const auto replayed = replay_all(archive, 9.0, 30.0);
    REQUIRE(replayed.size() == 3);
    CHECK(replayed[0].pitch == 62);
    CHECK(replayed[0].local_q == Catch::Approx(0.0).margin(1e-9));
    CHECK(replayed[1].pitch == 64);
    CHECK(replayed[2].pitch == 65);
}

TEST_CASE("the latest judgment at a position wins", "[scoreview][verdict-archive]")
{
    VerdictArchive archive;
    archive.record(4.0, 60, NoteVerdict::Missed);
    archive.record(4.0, 60, NoteVerdict::Correct); // re-judged on a review pass
    archive.record(4.0, 64, NoteVerdict::Missed); // another pitch keeps its own slot
    CHECK(archive.size() == 2);
    const auto replayed = replay_all(archive, 0.0, 10.0);
    REQUIRE(replayed.size() == 2);
    CHECK(replayed[0].pitch == 60);
    CHECK(replayed[0].verdict == NoteVerdict::Correct);
}

TEST_CASE("positions within the quantum collapse to one entry",
    "[scoreview][verdict-archive]")
{
    // The archive quantizes to thousandths of a quarter; two writes closer
    // than that are the same onset by construction (the judge's onsets are
    // far coarser than 1e-3 beats apart).
    VerdictArchive archive;
    archive.record(4.0001, 60, NoteVerdict::Missed);
    archive.record(4.0004, 60, NoteVerdict::Correct);
    CHECK(archive.size() == 1);

    archive.clear();
    CHECK(archive.empty());
}

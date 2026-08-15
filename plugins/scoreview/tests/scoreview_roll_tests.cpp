// Runner milestone R0 (plans/scoreview-runner.md): Roll mode — the
// transport never waits, notes judge in a timing window around their
// crossing, tempo eases from demonstrated accuracy. Synthetic onsets one
// quarter apart (marking 100 qpm, start tempo 60).

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>

#include <map>
#include <string>
#include <vector>

using namespace draxul::scoreview;

namespace
{

using Verdict = FlowController::NoteVerdict;

struct RollWorld
{
    FlowController flow;
    std::map<std::string, int> pitches;
};

RollWorld make_roll_world(int onset_count, std::map<std::string, int> pitch_overrides = {},
    std::map<std::string, std::vector<std::string>> extra_chord_ids = {},
    std::vector<std::string> tie_ends = {})
{
    RollWorld world;
    std::string json = "[";
    ScoreDrawList strip;
    strip.canvas_size = { static_cast<float>(onset_count) * 200.0f + 1000.0f, 500.0f };
    for (int i = 0; i < onset_count; ++i)
    {
        const std::string id = "n" + std::to_string(i);
        std::string ons = "\"" + id + "\"";
        world.pitches[id] = 60;
        GlyphInstance glyph;
        glyph.xform = Affine::translate(static_cast<float>(i) * 200.0f + 100.0f, 100.0f);
        glyph.element_id = id;
        strip.glyphs.push_back(glyph);
        for (const std::string& extra : extra_chord_ids[id])
        {
            ons += ", \"" + extra + "\"";
            world.pitches[extra] = 60;
            GlyphInstance chord_glyph = glyph;
            chord_glyph.element_id = extra;
            strip.glyphs.push_back(chord_glyph);
        }
        json += std::string(i > 0 ? "," : "") + "{\"qstamp\": " + std::to_string(i)
            + (i == 0 ? ", \"tempo\": 100" : "") + ", \"on\": [" + ons + "]}";
    }
    json += "]";
    for (auto& [id, pitch] : pitch_overrides)
        world.pitches[id] = pitch;

    std::string error;
    auto timemap = parse_timemap(json, error);
    REQUIRE(timemap.has_value());
    REQUIRE(world.flow.build(*timemap, strip, error));
    world.flow.prepare_gates(
        [&world](const std::string& id) {
            const auto found = world.pitches.find(id);
            return found == world.pitches.end() ? -1 : found->second;
        },
        tie_ends);
    world.flow.set_mode(FlowController::TransportMode::Roll);
    world.flow.play();
    return world;
}

// Advances in ~16 ms frames until position_q reaches at least `target_q`.
void roll_to(FlowController& flow, double target_q)
{
    for (int i = 0; i < 100000 && flow.position_q() < target_q; ++i)
        flow.advance(0.016);
}

void play(FlowController& flow, int pitch)
{
    flow.judge({ { pitch, 0.0 } });
}

Verdict verdict_of(const FlowController& flow, size_t onset, size_t note = 0)
{
    return flow.gates()[onset].notes[note].verdict;
}

} // namespace

TEST_CASE("roll never waits and misses pass by", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(6);
    CHECK_FALSE(world.flow.waiting());

    // No input at all: the transport reaches the end on its own...
    roll_to(world.flow, 100.0);
    CHECK(world.flow.at_end());
    CHECK_FALSE(world.flow.playing());
    // ...and every note is Missed, never stuck Pending.
    for (size_t i = 0; i < world.flow.gates().size(); ++i)
        CHECK(verdict_of(world.flow, i) == Verdict::Missed);
    CHECK(world.flow.miss_count() == 6);
    // Struggling drags the tempo down from the 60% start.
    CHECK(world.flow.tempo_qpm() < 60.0);
    CHECK(world.flow.tempo_qpm() >= world.flow.min_tempo_qpm());
}

TEST_CASE("roll judges pitches inside the timing window", "[scoreview][roll]")
{
    // Onset 2 gets a unique pitch so "too early" can't legally match the
    // previous onset's still-open late window.
    RollWorld world = make_roll_world(4, { { "n2", 72 } });

    // Too early for onset 2 (0.6 beats ahead): counts as a wrong note.
    roll_to(world.flow, 2.0 - 0.6);
    play(world.flow, 72);
    CHECK(world.flow.wrong_count() == 1);
    CHECK(verdict_of(world.flow, 2) == Verdict::Pending);

    // Inside the early window: correct.
    roll_to(world.flow, 2.0 - 0.3);
    play(world.flow, 72);
    CHECK(verdict_of(world.flow, 2) == Verdict::Correct);

    // Late but inside the window still lands (onset 3).
    roll_to(world.flow, 3.0 + 0.3);
    play(world.flow, 60);
    CHECK(verdict_of(world.flow, 3) == Verdict::Correct);
    CHECK(world.flow.miss_count() >= 2); // onsets 0 and 1 rolled past unplayed
}

TEST_CASE("roll resolves chords per note", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(3, { { "c1", 64 } }, { { "n1", { "c1" } } });

    roll_to(world.flow, 1.0);
    play(world.flow, 60); // one note of the chord only
    roll_to(world.flow, 2.5);
    CHECK(verdict_of(world.flow, 1, 0) == Verdict::Correct);
    CHECK(verdict_of(world.flow, 1, 1) == Verdict::Missed);
    CHECK(world.flow.wrong_count() == 0);
}

TEST_CASE("roll counts stray pitches as wrong notes", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(3);
    roll_to(world.flow, 1.0);
    play(world.flow, 61); // adjacent fluff
    play(world.flow, 60); // then the right note
    CHECK(world.flow.wrong_count() == 1);
    CHECK(verdict_of(world.flow, 1) == Verdict::Correct);
    CHECK(world.flow.streak() == 1); // wrong note reset, then rebuilt
}

TEST_CASE("roll auto-satisfies tie continuations and re-voicing is free", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(4, {}, {}, { "n2" });

    // Re-voice the tie continuation inside its window: correct, not wrong.
    roll_to(world.flow, 2.0);
    play(world.flow, 60);
    CHECK(verdict_of(world.flow, 2) == Verdict::Correct);
    CHECK(world.flow.wrong_count() == 0);

    // A different tie continuation left alone resolves Correct on its own.
    RollWorld alone = make_roll_world(4, {}, {}, { "n2" });
    roll_to(alone.flow, 3.0);
    CHECK(verdict_of(alone.flow, 2) == Verdict::Correct);
    CHECK(alone.flow.miss_count() >= 1); // the real notes still miss honestly
}

TEST_CASE("roll accuracy eases tempo toward the cap", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(30);
    const double start_qpm = world.flow.tempo_qpm();
    for (int i = 0; i < 30; ++i)
    {
        roll_to(world.flow, static_cast<double>(i));
        play(world.flow, 60);
    }
    roll_to(world.flow, 200.0); // resolve everything
    CHECK(world.flow.miss_count() == 0);
    CHECK(world.flow.tempo_qpm() > start_qpm + 4.0);
    CHECK(world.flow.tempo_qpm() <= world.flow.max_tempo_qpm());
    CHECK(world.flow.accuracy_ema() > 0.9);
}

TEST_CASE("roll primes the listener with in-window pitches", "[scoreview][roll]")
{
    RollWorld world = make_roll_world(5, { { "n3", 72 } });
    roll_to(world.flow, 3.0);
    const std::vector<int> pitches = world.flow.armed_required_pitches();
    REQUIRE(pitches.size() == 1);
    CHECK(pitches[0] == 72);
}

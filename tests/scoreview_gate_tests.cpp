// Gate milestone G0-G2 (plans/scoreview-gate.md): expected pitches and tie
// ends from the engine, gate transport + judgment + adaptive tempo + score
// in the FlowController, bot simulations, and verdict highlight states.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/bot_player_input.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace draxul::scoreview;

namespace
{

using Verdict = FlowController::NoteVerdict;

struct GateWorld
{
    FlowController flow;
    std::map<std::string, int> pitches;
};

// n gates, one quarter apart, ids g0..g(n-1); pitches from the map (default
// 60). tie_ends marks ids auto-satisfied.
GateWorld make_gate_world(int gate_count, std::map<std::string, int> pitch_overrides = {},
    std::map<std::string, std::vector<std::string>> extra_chord_ids = {},
    std::vector<std::string> tie_ends = {})
{
    GateWorld world;
    std::string json = "[";
    ScoreDrawList strip;
    strip.canvas_size = { static_cast<float>(gate_count) * 200.0f + 1000.0f, 500.0f };
    for (int i = 0; i < gate_count; ++i)
    {
        const std::string id = "g" + std::to_string(i);
        std::string ons = "\"" + id + "\"";
        world.pitches[id] = 60;
        GlyphInstance glyph;
        glyph.xform = Affine::translate(static_cast<float>(i) * 200.0f + 100.0f, 100.0f);
        glyph.element_id = id;
        glyph.symbol_index = 0;
        strip.glyphs.push_back(glyph);
        for (const std::string& extra : extra_chord_ids[id])
        {
            ons += ", \"" + extra + "\"";
            world.pitches[extra] = 60;
            GlyphInstance chord_glyph = glyph;
            chord_glyph.element_id = extra;
            strip.glyphs.push_back(chord_glyph);
        }
        json += std::string(i > 0 ? "," : "") + "{\"qstamp\": " + std::to_string(i) + (i == 0 ? ", \"tempo\": 100" : "") + ", \"on\": [" + ons + "]}";
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
    world.flow.set_mode(FlowController::TransportMode::Gate);
    return world;
}

PlayerNoteEvent note(int pitch, double t)
{
    PlayerNoteEvent event;
    event.midi_pitch = pitch;
    event.t_seconds = t;
    return event;
}

} // namespace

TEST_CASE("engine reports pitches and tie ends for the live Grieg", "[scoreview][gate]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    std::ifstream stream(
        std::string(DRAXUL_PROJECT_ROOT) + "/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl",
        std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    REQUIRE(engine->load(buffer.str(), error));

    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    std::set<std::string> ons;
    for (const TimemapEntry& entry : timemap->entries)
        ons.insert(entry.note_on.begin(), entry.note_on.end());
    REQUIRE(ons.size() == 645);

    // Every sounding note has a plausible piano pitch (G0 ground truth).
    for (const std::string& id : ons)
    {
        const int pitch = engine->midi_pitch_for_element(id);
        CHECK(pitch >= 21);
        CHECK(pitch <= 108);
    }
    CHECK(engine->midi_pitch_for_element("no-such-id") == -1);

    // 18 tie elements, of which 17 carry an endid — the fixture famously has
    // one unterminated tie (Verovio warns "1 ties left open" at import).
    const std::vector<std::string> tie_ends = engine->tie_end_ids();
    CHECK(tie_ends.size() == 17);
    for (const std::string& id : tie_ends)
        CHECK(ons.count(id) == 1);
}

TEST_CASE("gate opens on the correct note and scores a streak", "[scoreview][gate]")
{
    GateWorld world = make_gate_world(3);
    FlowController& flow = world.flow;
    REQUIRE(flow.gates_ready());
    flow.play();
    flow.advance(1.0 / 60.0);
    CHECK(flow.waiting()); // playhead starts on gate 0
    CHECK(flow.armed_required_pitches() == std::vector<int>{ 60 });

    flow.judge({ note(60, 1.0) });
    CHECK(flow.armed_gate_index() == 1);
    CHECK(flow.streak() == 1);
    CHECK(flow.score() > 0);
    CHECK(flow.miss_count() == 0);

    auto update = flow.take_verdict_update();
    // reset arrives from mode/prepare; the change list carries the verdict.
    REQUIRE_FALSE(update.changes.empty());
    CHECK(update.changes.back().first == "g0");
    CHECK(update.changes.back().second == Verdict::Correct);

    // Position glides to the next gate and waits again.
    flow.advance(10.0);
    CHECK(flow.position_q() == Catch::Approx(1.0));
    CHECK(flow.waiting());
}

TEST_CASE("wrong notes open an attempted gate as missed and reset the streak",
    "[scoreview][gate]")
{
    GateWorld world = make_gate_world(2);
    FlowController& flow = world.flow;
    flow.play();
    flow.advance(0.1);
    flow.judge({ note(60, 0.5) });
    CHECK(flow.streak() == 1);

    flow.advance(10.0); // glide to gate 1
    flow.judge({ note(61, 1.5) }); // wrong pitch: attempted -> Missed, move on
    CHECK(flow.armed_gate_index() == 2);
    CHECK(flow.streak() == 0);
    CHECK(flow.miss_count() == 1);

    const auto update = flow.take_verdict_update();
    bool saw_missed = false;
    for (const auto& [id, verdict] : update.changes)
        saw_missed |= (id == "g1" && verdict == Verdict::Missed);
    CHECK(saw_missed);
}

TEST_CASE("chords need every pitch, duplicates counted by multiset", "[scoreview][gate]")
{
    // Gate g0 is a three-note chord: 60, 64, and a doubled 60.
    GateWorld world = make_gate_world(2, { { "c1", 64 }, { "c2", 60 } },
        { { "g0", { "c1", "c2" } } });
    FlowController& flow = world.flow;
    flow.play();
    flow.advance(0.1);
    REQUIRE(flow.armed_required_pitches().size() == 3);

    flow.judge({ note(60, 0.2) });
    CHECK(flow.armed_gate_index() == 0); // one 60 matched, one still pending
    flow.judge({ note(64, 0.3) });
    CHECK(flow.armed_gate_index() == 0);
    flow.judge({ note(60, 0.4) }); // the doubled 60
    CHECK(flow.armed_gate_index() == 1);
    CHECK(flow.streak() == 1);
    CHECK(flow.miss_count() == 0);
}

TEST_CASE("partial chords with wrong fills open with mixed verdicts", "[scoreview][gate]")
{
    GateWorld world = make_gate_world(2, { { "c1", 64 } }, { { "g0", { "c1" } } });
    FlowController& flow = world.flow;
    flow.play();
    flow.advance(0.1);
    flow.judge({ note(60, 0.2), note(59, 0.3) }); // one right, one wrong = attempted
    CHECK(flow.armed_gate_index() == 1);
    CHECK(flow.miss_count() == 1); // the 64 went unmatched
    CHECK(flow.streak() == 0);

    const auto update = flow.take_verdict_update();
    std::map<std::string, Verdict> verdicts;
    for (const auto& [id, verdict] : update.changes)
        verdicts[id] = verdict;
    CHECK(verdicts["g0"] == Verdict::Correct);
    CHECK(verdicts["c1"] == Verdict::Missed);
}

TEST_CASE("tie continuations auto-open and re-voicing them is free", "[scoreview][gate]")
{
    // g1 is a pure tie continuation; g2 mixes a required note with a tied one.
    GateWorld world = make_gate_world(4, { { "t1", 62 } }, { { "g2", { "t1" } } },
        { "g1", "t1" });
    FlowController& flow = world.flow;
    flow.play();
    flow.advance(0.1);
    flow.judge({ note(60, 0.2) }); // open gate 0

    // Gate 1 is auto: the glide crosses it without waiting.
    flow.advance(10.0);
    CHECK(flow.armed_gate_index() == 2);
    CHECK(flow.waiting());

    // Re-voicing the tied note first must not blow the gate...
    flow.judge({ note(62, 2.0) });
    CHECK(flow.armed_gate_index() == 2);
    CHECK(flow.miss_count() == 0);
    // ...and the required note still opens it cleanly.
    flow.judge({ note(60, 2.2) });
    CHECK(flow.armed_gate_index() == 3);
    CHECK(flow.miss_count() == 0);
    CHECK(flow.streak() == 2); // gates 0 and 2 (auto gate 1 doesn't score)

    const auto update = flow.take_verdict_update();
    std::map<std::string, Verdict> verdicts;
    for (const auto& [id, verdict] : update.changes)
        verdicts[id] = verdict;
    CHECK(verdicts["g1"] == Verdict::Correct); // auto-opened
    CHECK(verdicts["t1"] == Verdict::Correct); // re-voiced
    CHECK(verdicts["g2"] == Verdict::Correct);
}

TEST_CASE("player pace eases the tempo and stalls decay it", "[scoreview][gate]")
{
    GateWorld world = make_gate_world(24);
    FlowController& flow = world.flow;
    flow.play();
    const double start_tempo = flow.tempo_qpm();
    CHECK(start_tempo == Catch::Approx(60.0)); // marking 100 x 0.6

    // Play one gate per 1.2 seconds = 50 qpm demonstrated pace. Advance in
    // frame-sized steps (as the host does) so the glide reaches each gate
    // without accumulating a stall beyond the grace window.
    double t = 0.5;
    for (int i = 0; i < 16; ++i)
    {
        for (int step = 0; step < 120; ++step)
            flow.advance(1.0 / 60.0); // 2s of frames: arrive and wait briefly
        flow.judge({ note(60, t) });
        t += 1.2;
    }
    CHECK(flow.tempo_qpm() < 55.0);
    CHECK(flow.tempo_qpm() > 45.0);

    // Stalling at a gate beyond the grace window decays tempo further.
    const double before_stall = flow.tempo_qpm();
    flow.advance(10.0);
    for (int i = 0; i < 600; ++i)
        flow.advance(1.0 / 60.0); // ten silent seconds at the gate
    CHECK(flow.tempo_qpm() < before_stall);
    CHECK(flow.tempo_qpm() >= flow.min_tempo_qpm());
}

TEST_CASE("bot simulations converge tempo and accumulate misses", "[scoreview][gate]")
{
    SECTION("perfect bot at 50 qpm pulls tempo down to its pace")
    {
        GateWorld world = make_gate_world(40);
        FlowController& flow = world.flow;
        BotPlayerInput bot(flow, 50.0, 1.0, 42);
        flow.play();
        double t = 0.0;
        std::vector<PlayerNoteEvent> events;
        while (flow.playing() && t < 120.0)
        {
            t += 1.0 / 60.0;
            flow.advance(1.0 / 60.0);
            events.clear();
            bot.poll(t, events);
            flow.judge(events);
        }
        CHECK(flow.at_end());
        CHECK(flow.miss_count() == 0);
        CHECK(flow.score() > 0);
        CHECK(flow.tempo_qpm() == Catch::Approx(50.0).margin(6.0));
    }

    SECTION("fast bot pushes tempo up but never past the marking cap")
    {
        GateWorld world = make_gate_world(40);
        FlowController& flow = world.flow;
        BotPlayerInput bot(flow, 160.0, 1.0, 7);
        flow.play();
        double t = 0.0;
        std::vector<PlayerNoteEvent> events;
        while (flow.playing() && t < 120.0)
        {
            t += 1.0 / 60.0;
            flow.advance(1.0 / 60.0);
            events.clear();
            bot.poll(t, events);
            flow.judge(events);
        }
        CHECK(flow.at_end());
        CHECK(flow.tempo_qpm() > 90.0);
        CHECK(flow.tempo_qpm() <= flow.max_tempo_qpm() + 0.001);
    }

    SECTION("an erratic bot accumulates misses and streak resets")
    {
        GateWorld world = make_gate_world(40);
        FlowController& flow = world.flow;
        BotPlayerInput bot(flow, 80.0, 0.7, 1234);
        flow.play();
        double t = 0.0;
        std::vector<PlayerNoteEvent> events;
        while (flow.playing() && t < 120.0)
        {
            t += 1.0 / 60.0;
            flow.advance(1.0 / 60.0);
            events.clear();
            bot.poll(t, events);
            flow.judge(events);
        }
        CHECK(flow.at_end());
        CHECK(flow.miss_count() > 0);
        CHECK(flow.streak() < 40);
        CHECK(flow.score() > 0);
    }
}

TEST_CASE("rewind resets gates, verdicts, and the score", "[scoreview][gate]")
{
    GateWorld world = make_gate_world(4);
    FlowController& flow = world.flow;
    flow.play();
    flow.advance(0.1);
    flow.judge({ note(60, 0.5) });
    flow.take_verdict_update();
    REQUIRE(flow.score() > 0);

    flow.rewind();
    CHECK(flow.score() == 0);
    CHECK(flow.streak() == 0);
    CHECK(flow.miss_count() == 0);
    CHECK(flow.armed_gate_index() == 0);
    const auto update = flow.take_verdict_update();
    CHECK(update.reset);
    CHECK(update.changes.empty());
}

TEST_CASE("highlight states carry verdict colors", "[scoreview][gate]")
{
    ScoreDrawList list;
    list.canvas_size = { 1000.0f, 100.0f };
    GlyphInstance glyph;
    glyph.element_id = "n1";
    list.glyphs.push_back(glyph);
    glyph.element_id = "n2";
    list.glyphs.push_back(glyph);

    ScoreHighlightState highlight;
    highlight.build(list);
    CHECK(highlight.set_state("n1", ScoreHighlightState::State::Correct));
    CHECK(highlight.set_state("n2", ScoreHighlightState::State::Missed));
    CHECK(highlight.glyph_lit[0] == static_cast<uint8_t>(ScoreHighlightState::State::Correct));
    CHECK(highlight.glyph_lit[1] == static_cast<uint8_t>(ScoreHighlightState::State::Missed));
    CHECK(highlight.set_lit("n1")); // clock-mode compatibility: Passed
    CHECK(highlight.glyph_lit[0] == static_cast<uint8_t>(ScoreHighlightState::State::Passed));
    highlight.clear_lit();
    CHECK(highlight.glyph_lit[0] == 0);
}

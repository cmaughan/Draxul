// Stream milestone S0 (plans/scoreview-stream.md): the player's memory —
// outcome aggregation, recent-encounter mastery, chord trouble, timing
// statistics, versioned JSON round-trip with unknown-field preservation,
// and the atomic progress store.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/progress_store.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>

#include "support/temp_dir.h"

#include <cmath>
#include <string>

using namespace draxul::scoreview;

namespace
{

NoteOutcome hit(double onset_q, int pitch, double delta_q, double quality)
{
    NoteOutcome outcome;
    outcome.id = "n";
    outcome.onset_q = onset_q;
    outcome.pitch = pitch;
    outcome.verdict = NoteVerdict::Correct;
    outcome.delta_q = delta_q;
    outcome.quality = quality;
    return outcome;
}

NoteOutcome miss(double onset_q, int pitch)
{
    NoteOutcome outcome;
    outcome.id = "n";
    outcome.onset_q = onset_q;
    outcome.pitch = pitch;
    outcome.verdict = NoteVerdict::Missed;
    return outcome;
}

NoteOutcome hand_note(double onset_q, int pitch, int staff, bool correct)
{
    NoteOutcome outcome;
    outcome.id = "n";
    outcome.onset_q = onset_q;
    outcome.pitch = pitch;
    outcome.staff = staff;
    outcome.verdict = correct ? NoteVerdict::Correct : NoteVerdict::Missed;
    outcome.quality = correct ? 1.0 : 0.0;
    return outcome;
}

NoteOutcome stray(int pitch)
{
    NoteOutcome outcome;
    outcome.pitch = pitch;
    outcome.stray = true;
    return outcome;
}

} // namespace

TEST_CASE("player model aggregates outcomes into stats", "[scoreview][player-model]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    model.begin_session("2026-07-13T10:00:00Z");

    model.apply(hit(0.0, 60, -0.10, 0.9));
    model.apply(hit(0.0, 60, 0.10, 0.8));
    model.apply(miss(1.0, 62));
    model.apply(stray(61)); // a semitone off 60 and 62: near-wrong for both

    const auto& p60 = model.pitch_stats().at(60);
    CHECK(p60.hit == 2);
    CHECK(p60.miss == 0);
    CHECK(p60.wrong_near == 1);
    CHECK(p60.timing.samples == 2);
    CHECK(p60.timing.mean_q == Catch::Approx(0.0).margin(1e-9));
    CHECK(p60.timing.variance_q() == Catch::Approx(0.02).epsilon(0.01));
    CHECK(model.pitch_stats().at(62).wrong_near == 1);

    // Bar 0 (onsets 0..3 at 3 quarters/bar) blends both onsets' recents.
    CHECK(model.onset_stats().at(0.0).recent_mean() == Catch::Approx(0.85));
    CHECK(model.onset_stats().at(1.0).recent_mean() == 0.0);
    CHECK(model.bar_mastery(0) == Catch::Approx((0.85 + 0.0) / 2.0));

    ChordOutcome chord;
    chord.onset_q = 2.0;
    chord.pitches = { 48, 64 };
    chord.result = ChordOutcome::Result::Split;
    model.apply(chord);
    CHECK(model.chord_stats().at("48+64").split == 1);

    model.end_session(120, 0.74);
    CHECK(model.sessions().size() == 1);
    CHECK(model.sessions().back().notes == 4);
    CHECK(model.last_tempo_frac() == Catch::Approx(0.74));
    CHECK(model.best_tempo_frac() == Catch::Approx(0.74));
}

TEST_CASE("player model round-trips through JSON", "[scoreview][player-model]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    model.begin_session("2026-07-13T10:00:00Z");
    for (int i = 0; i < 10; ++i)
        model.apply(hit(0.5, 60, 0.05 * i, 1.0 - 0.05 * i));
    model.apply(miss(1.5, 64));
    model.end_session(60, 0.8);

    const std::string json_text = model.serialize();
    PlayerModel restored;
    REQUIRE(restored.deserialize(json_text));
    CHECK(restored.pitch_stats().at(60).hit == 10);
    CHECK(restored.pitch_stats().at(60).timing.mean_q
        == Catch::Approx(model.pitch_stats().at(60).timing.mean_q));
    CHECK(restored.pitch_stats().at(60).timing.variance_q()
        == Catch::Approx(model.pitch_stats().at(60).timing.variance_q()));
    // The recent ring keeps only the last kRecentEncounters entries.
    CHECK(restored.onset_stats().at(0.5).recent.size()
        == static_cast<size_t>(PlayerModel::kRecentEncounters));
    CHECK(restored.sessions().size() == 1);
    CHECK(restored.best_tempo_frac() == Catch::Approx(0.8));
    CHECK(restored.total_notes_judged() == 11);
    // Continuing after a reload must extend, not reset, the statistics.
    restored.begin_session("2026-07-13T11:00:00Z");
    restored.apply(hit(0.5, 60, 0.0, 1.0));
    CHECK(restored.pitch_stats().at(60).hit == 11);
}

TEST_CASE("player model preserves unknown JSON fields", "[scoreview][player-model]")
{
    PlayerModel model;
    REQUIRE(model.deserialize(R"({
        "version": 1,
        "total_notes": 5,
        "future_field": {"from": "a newer build"},
        "pitch": {"60": {"hit": 5}}
    })"));
    CHECK(model.total_notes_judged() == 5);
    CHECK(model.pitch_stats().at(60).hit == 5);
    const std::string out = model.serialize();
    CHECK(out.find("future_field") != std::string::npos);
    CHECK(out.find("a newer build") != std::string::npos);
}

TEST_CASE("player model rejects corrupt JSON without crashing", "[scoreview][player-model]")
{
    PlayerModel model;
    CHECK_FALSE(model.deserialize("not json at all {"));
    CHECK_FALSE(model.deserialize("[1,2,3]"));
    CHECK(model.total_notes_judged() == 0);
}

TEST_CASE("progress store hashes, saves atomically, and loads", "[scoreview][player-model]")
{
    draxul::tests::TempDir dir("scoreview-progress");

    const std::string bytes_a = "<score>piece A</score>";
    const std::string bytes_b = "<score>piece B</score>";
    CHECK(piece_hash(bytes_a) == piece_hash(bytes_a)); // stable
    CHECK(piece_hash(bytes_a) != piece_hash(bytes_b)); // distinct

    const auto path = progress_path(dir.path / "progress", bytes_a);
    CHECK(load_progress(path).empty()); // fresh start, not an error

    std::string error;
    REQUIRE(save_progress_atomic(path, R"({"version":1})", error));
    CHECK(load_progress(path) == R"({"version":1})");
    // Overwrite goes through the tmp+rename path too.
    REQUIRE(save_progress_atomic(path, R"({"version":1,"x":2})", error));
    CHECK(load_progress(path) == R"({"version":1,"x":2})");
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
}

TEST_CASE("roll hits report timing deltas and quality to the model", "[scoreview][player-model]")
{
    // End-to-end through the FlowController: a sloppy (late) hit must carry
    // a positive delta and a lower quality than a centered hit.
    // (Fixture mirrors the roll tests: onsets one quarter apart, 100 qpm.)
    // Built inline to keep this file self-contained.
    ScoreDrawList strip;
    strip.canvas_size = { 1400.0f, 500.0f };
    std::string json = "[";
    for (int i = 0; i < 3; ++i)
    {
        GlyphInstance glyph;
        glyph.xform = Affine::translate(static_cast<float>(i) * 200.0f + 100.0f, 100.0f);
        glyph.element_id = "n" + std::to_string(i);
        strip.glyphs.push_back(glyph);
        json += std::string(i > 0 ? "," : "") + "{\"qstamp\": " + std::to_string(i)
            + (i == 0 ? ", \"tempo\": 100" : "") + ", \"on\": [\"n" + std::to_string(i)
            + "\"]}";
    }
    json += "]";
    std::string error;
    auto timemap = parse_timemap(json, error);
    REQUIRE(timemap.has_value());
    FlowController flow;
    REQUIRE(flow.build(*timemap, strip, error));
    flow.prepare_gates([](const std::string&) { return 60; }, {});
    flow.set_mode(FlowController::TransportMode::Roll);
    flow.play();

    const auto roll_to = [&flow](double target) {
        for (int i = 0; i < 100000 && flow.position_q() < target; ++i)
            flow.advance(0.016);
    };

    roll_to(1.0); // centered on onset 1
    flow.judge({ { 60, 0.0 } });
    roll_to(2.0 + 0.35); // late on onset 2
    flow.judge({ { 60, 0.0 } });
    roll_to(10.0);

    const auto outcomes = flow.take_note_outcomes();
    REQUIRE(outcomes.size() >= 3); // hit, hit, plus onset 0's miss
    const NoteOutcome* centered = nullptr;
    const NoteOutcome* late = nullptr;
    for (const auto& outcome : outcomes)
    {
        if (outcome.verdict != NoteVerdict::Correct)
            continue;
        if (outcome.onset_q == 1.0)
            centered = &outcome;
        if (outcome.onset_q == 2.0)
            late = &outcome;
    }
    REQUIRE(centered != nullptr);
    REQUIRE(late != nullptr);
    CHECK(std::abs(centered->delta_q) < 0.05);
    CHECK(late->delta_q > 0.3);
    CHECK(centered->quality > 0.95);
    CHECK(late->quality < 0.7);
    CHECK(late->quality >= FlowController::kRollEdgeQuality);
}

TEST_CASE("bar tally tracks per-bar and per-hand right/wrong", "[scoreview][player-model]")
{
    PlayerModel model;
    model.set_piece("Walz", 120.0, 3.0); // 3 quarters per bar
    model.apply(hit(0.0, 64, 0.0, 1.0)); // bar 0, right hand (>= middle C), ok
    model.apply(miss(1.0, 48)); // bar 0, left hand (< middle C), wrong
    model.apply(hit(2.0, 72, 0.0, 1.0)); // bar 0, right hand, ok
    model.apply(miss(3.0, 50)); // bar 1 (onset 3 / 3), left hand, wrong
    model.apply(miss(PlayerModel::kDrillOnsetSentinel, 60)); // drill: no bar location

    const auto& bars = model.bar_tally();
    REQUIRE(bars.size() == 2); // bars 0 and 1; the drill contributes no bar
    CHECK(bars.at(0).hit == 2);
    CHECK(bars.at(0).miss == 1);
    CHECK(bars.at(0).right.hit == 2);
    CHECK(bars.at(0).right.miss == 0);
    CHECK(bars.at(0).left.hit == 0);
    CHECK(bars.at(0).left.miss == 1);
    CHECK(bars.at(1).miss == 1);
    CHECK(bars.at(1).left.miss == 1);
    CHECK(model.bar_hand_last_pass_dirty(0, 2));
    CHECK_FALSE(model.bar_hand_last_pass_dirty(0, 1));
    CHECK(model.bar_hand_pass_count(0, 2) == 1);
    CHECK(model.bar_hand_pass_count(0, 1) == 1);
    CHECK(model.bar_hand_consecutive_clean(0, 1) == 1);
    CHECK(model.bar_hand_consecutive_clean(0, 2) == 0);

    // Survives the versioned JSON round-trip.
    PlayerModel loaded;
    REQUIRE(loaded.deserialize(model.serialize()));
    CHECK(loaded.bar_tally().at(0).right.hit == 2);
    CHECK(loaded.bar_tally().at(1).left.miss == 1);
    CHECK(loaded.bar_hand_pass_count(0, 2) == 1);
    CHECK(loaded.bar_hand_consecutive_clean(0, 1) == 1);

    // clear_progress wipes the record but keeps piece identity.
    model.clear_progress();
    CHECK(model.bar_tally().empty());
    CHECK(model.pitch_stats().empty());
    CHECK(model.total_notes_judged() == 0);
}

TEST_CASE("per-hand pass state clears only after that hand plays cleanly",
    "[scoreview][player-model]")
{
    PlayerModel model;
    model.set_piece("Walz", 120.0, 3.0);
    model.apply(hand_note(0.0, 48, 2, false));
    model.apply(hand_note(1.0, 64, 1, true));
    model.close_open_pass();
    CHECK(model.bar_hand_last_pass_dirty(0, 2));
    CHECK_FALSE(model.bar_hand_last_pass_dirty(0, 1));

    model.apply(hand_note(0.0, 48, 2, true));
    model.close_open_pass();
    CHECK_FALSE(model.bar_hand_last_pass_dirty(0, 2));
    CHECK(model.bar_hand_consecutive_clean(0, 2) == 1);
    CHECK(model.bar_hand_pass_count(0, 2) == 2);
    CHECK(model.bar_hand_pass_count(0, 1) == 1);
}

// --- Complete-pass tracking (C3, plans/scoreview-composer.md) --------------
// Promotion reads consecutive CLEAN COMPLETE traversals, never the mean of
// note qualities — a chronically fumbled note must block promotion forever.

namespace
{

draxul::scoreview::NoteOutcome pass_outcome(double onset_q, bool correct)
{
    draxul::scoreview::NoteOutcome outcome;
    outcome.onset_q = onset_q;
    outcome.pitch = 60;
    outcome.verdict = correct ? draxul::scoreview::NoteVerdict::Correct
                              : draxul::scoreview::NoteVerdict::Missed;
    outcome.quality = correct ? 1.0 : 0.0;
    return outcome;
}

} // namespace

TEST_CASE("bar transitions close complete passes, clean or fumbled",
    "[scoreview][player-model]")
{
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);

    // Bar 0 clean, bar 1 with one miss, back to bar 0 clean again (a review
    // slot) — each transition closes the previous traversal.
    for (int beat = 0; beat < 4; ++beat)
        model.apply(pass_outcome(beat, true));
    for (int beat = 0; beat < 4; ++beat)
        model.apply(pass_outcome(4.0 + beat, beat != 2));
    for (int beat = 0; beat < 4; ++beat)
        model.apply(pass_outcome(beat, true));
    model.close_open_pass();

    CHECK(model.bar_pass_count(0) == 2);
    CHECK(model.bar_consecutive_clean(0) == 2);
    CHECK_FALSE(model.bar_last_pass_dirty(0));
    CHECK(model.bar_pass_count(1) == 1);
    CHECK(model.bar_consecutive_clean(1) == 0);
    CHECK(model.bar_last_pass_dirty(1));
}

TEST_CASE("a stray fumbles the pass in flight", "[scoreview][player-model]")
{
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    model.apply(pass_outcome(0.0, true));
    draxul::scoreview::NoteOutcome stray;
    stray.onset_q = 1.0; // transport position inside bar 0
    stray.pitch = 61;
    stray.stray = true;
    model.apply(stray);
    model.apply(pass_outcome(2.0, true));
    model.close_open_pass();

    CHECK(model.bar_pass_count(0) == 1);
    CHECK(model.bar_consecutive_clean(0) == 0);
    CHECK(model.bar_last_pass_dirty(0));
}

TEST_CASE("a fumble resets the clean streak; drills never touch it",
    "[scoreview][player-model]")
{
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    const auto sweep_bar0 = [&](bool clean) {
        for (int beat = 0; beat < 4; ++beat)
            model.apply(pass_outcome(beat, clean || beat != 1));
        // A drill slot between traversals: sentinel onsets, no bar.
        draxul::scoreview::NoteOutcome drill;
        drill.onset_q = draxul::scoreview::PlayerModel::kDrillOnsetSentinel;
        drill.pitch = 60;
        drill.verdict = draxul::scoreview::NoteVerdict::Missed;
        model.apply(drill);
        // Another bar closes bar 0's pass.
        model.apply(pass_outcome(4.0, true));
    };
    sweep_bar0(true);
    sweep_bar0(true);
    CHECK(model.bar_consecutive_clean(0) == 2);
    sweep_bar0(false);
    CHECK(model.bar_consecutive_clean(0) == 0);
    sweep_bar0(true);
    CHECK(model.bar_consecutive_clean(0) == 1);
    CHECK(model.bar_pass_count(0) == 4);
}

TEST_CASE("pass rings survive the progress round-trip", "[scoreview][player-model]")
{
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    for (int beat = 0; beat < 4; ++beat)
        model.apply(pass_outcome(beat, true));
    for (int beat = 0; beat < 4; ++beat)
        model.apply(pass_outcome(4.0 + beat, beat != 0));
    model.close_open_pass();

    draxul::scoreview::PlayerModel restored;
    REQUIRE(restored.deserialize(model.serialize()));
    CHECK(restored.bar_pass_count(0) == 1);
    CHECK(restored.bar_consecutive_clean(0) == 1);
    CHECK(restored.bar_pass_count(1) == 1);
    CHECK(restored.bar_last_pass_dirty(1));
}

TEST_CASE("the tempo ladder climbs on clean passes and falls on fumbles",
    "[scoreview][player-model]")
{
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    using PM = draxul::scoreview::PlayerModel;
    CHECK(model.bar_tempo_ladder(0) == Catch::Approx(PM::kLadderStart));

    const auto pass = [&](bool clean) {
        for (int beat = 0; beat < 4; ++beat)
            model.apply(pass_outcome(beat, clean || beat != 2));
        model.close_open_pass();
    };
    pass(true);
    CHECK(model.bar_tempo_ladder(0)
        == Catch::Approx(PM::kLadderStart + PM::kLadderStep));
    pass(true);
    pass(true);
    const double after_three = model.bar_tempo_ladder(0);
    CHECK(after_three == Catch::Approx(PM::kLadderStart + 3 * PM::kLadderStep));
    pass(false);
    CHECK(model.bar_tempo_ladder(0) == Catch::Approx(after_three - PM::kLadderDrop));
    // The floor holds under repeated fumbles; the cap under endless success.
    for (int i = 0; i < 20; ++i)
        pass(false);
    CHECK(model.bar_tempo_ladder(0) == Catch::Approx(PM::kLadderFloor));
    for (int i = 0; i < 40; ++i)
        pass(true);
    CHECK(model.bar_tempo_ladder(0) == Catch::Approx(PM::kLadderCap));

    // And it survives the progress round-trip.
    draxul::scoreview::PlayerModel restored;
    REQUIRE(restored.deserialize(model.serialize()));
    CHECK(restored.bar_tempo_ladder(0) == Catch::Approx(PM::kLadderCap));
}

// --- Day-scale spacing (C4) --------------------------------------------------

TEST_CASE("civil days parse from session timestamps", "[scoreview][player-model]")
{
    using PM = draxul::scoreview::PlayerModel;
    const int day = PM::civil_day_from_iso("2026-07-17T10:30:00");
    CHECK(day > 0);
    CHECK(PM::civil_day_from_iso("2026-07-18T01:00:00") == day + 1);
    CHECK(PM::civil_day_from_iso("2026-08-17") == day + 31);
    CHECK(PM::civil_day_from_iso("garbage") == 0);
    CHECK(PM::civil_day_from_iso("") == 0);
}

TEST_CASE("the spaced streak counts day-separated clean re-encounters",
    "[scoreview][player-model]")
{
    // Same-day repetition must NOT advance the streak: minute-scale spacing
    // demonstrably teaches nothing (no forgetting happens in the gap).
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    const auto pass = [&](bool clean) {
        for (int beat = 0; beat < 4; ++beat)
            model.apply(pass_outcome(beat, clean || beat != 2));
        model.close_open_pass();
    };
    model.begin_session("2026-07-14T09:00:00");
    pass(true);
    pass(true); // same day: no streak movement
    model.end_session(60, 0.6);
    CHECK(model.bar_spaced_streak(0) == 0);

    model.begin_session("2026-07-15T09:00:00");
    pass(true);
    model.end_session(60, 0.6);
    CHECK(model.bar_spaced_streak(0) == 1);

    model.begin_session("2026-07-17T09:00:00");
    pass(true);
    model.end_session(60, 0.6);
    CHECK(model.bar_spaced_streak(0) == 2);

    model.begin_session("2026-07-18T09:00:00");
    pass(false); // a fumble resets the spacing credit
    model.end_session(60, 0.6);
    CHECK(model.bar_spaced_streak(0) == 0);
    CHECK(model.bar_last_pass_day(0)
        == draxul::scoreview::PlayerModel::civil_day_from_iso("2026-07-18"));

    draxul::scoreview::PlayerModel restored;
    REQUIRE(restored.deserialize(model.serialize()));
    CHECK(restored.bar_last_pass_day(0) == model.bar_last_pass_day(0));
    CHECK(restored.bar_spaced_streak(0) == 0);
}

TEST_CASE("the engraved staff outranks the pitch split for hand tallies",
    "[scoreview][player-model]")
{
    // C5: a low melody note on the UPPER staff belongs to the right hand —
    // the score says so; middle C is only the fallback.
    draxul::scoreview::PlayerModel model;
    model.set_piece("t", 120.0, 4.0);
    draxul::scoreview::NoteOutcome low_rh;
    low_rh.onset_q = 0.0;
    low_rh.pitch = 48; // far below middle C
    low_rh.staff = 1; // but engraved on the upper staff
    low_rh.verdict = draxul::scoreview::NoteVerdict::Correct;
    low_rh.quality = 1.0;
    model.apply(low_rh);
    draxul::scoreview::NoteOutcome high_lh;
    high_lh.onset_q = 1.0;
    high_lh.pitch = 72; // above middle C
    high_lh.staff = 2; // engraved on the lower staff (hand crossing)
    high_lh.verdict = draxul::scoreview::NoteVerdict::Missed;
    model.apply(high_lh);
    draxul::scoreview::NoteOutcome fallback;
    fallback.onset_q = 2.0;
    fallback.pitch = 40; // staff unknown: the pitch split applies
    fallback.verdict = draxul::scoreview::NoteVerdict::Correct;
    fallback.quality = 1.0;
    model.apply(fallback);

    const auto& tally = model.bar_tally().at(0);
    CHECK(tally.right.hit == 1); // the low RH note
    CHECK(tally.left.miss == 1); // the crossed LH note
    CHECK(tally.left.hit == 1); // the fallback low note
}

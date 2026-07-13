// Stream milestone S3 (plans/scoreview-stream.md): the composer — program
// planning from the player model (piece / review / drill), fabricated
// chord-drill bars engraved through the same window path, provenance
// geometry, and the drill sentinel in the player model.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/stream_composer.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace draxul::scoreview;

namespace
{

std::string read_grieg_xml()
{
    std::ifstream stream(std::string(DRAXUL_PROJECT_ROOT)
            + "/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.musicxml",
        std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

SourceSlicer& grieg_slicer()
{
    static SourceSlicer slicer;
    static bool loaded = false;
    if (!loaded)
    {
        std::string error;
        REQUIRE(slicer.load(read_grieg_xml(), error));
        loaded = true;
    }
    return slicer;
}

std::map<double, std::vector<int>> engrave_onsets(const std::string& xml)
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(xml, error));
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    std::map<double, std::vector<int>> onsets;
    for (const TimemapEntry& entry : timemap->entries)
    {
        for (const std::string& id : entry.note_on)
        {
            const int pitch = engine->midi_pitch_for_element(id);
            if (pitch >= 0)
                onsets[entry.qstamp].push_back(pitch);
        }
    }
    for (auto& [q, pitches] : onsets)
        std::sort(pitches.begin(), pitches.end());
    return onsets;
}

void add_chord_trouble(PlayerModel& model, std::vector<int> pitches, int misses)
{
    FlowController::ChordOutcome chord;
    chord.pitches = std::move(pitches);
    chord.result = FlowController::ChordOutcome::Result::Miss;
    for (int i = 0; i < misses; ++i)
        model.apply(chord);
}

void add_weak_bar(PlayerModel& model, int bar, double quarters_per_bar)
{
    for (int beat = 0; beat < 3; ++beat)
    {
        FlowController::NoteOutcome outcome;
        outcome.onset_q = bar * quarters_per_bar + beat;
        outcome.pitch = 60;
        outcome.verdict = FlowController::NoteVerdict::Missed;
        model.apply(outcome);
    }
}

} // namespace

TEST_CASE("a fabricated drill bar engraves inside a composed window", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);

    const std::string drill = composer.fabricate_chord_drill("52+60", 0);
    REQUIRE_FALSE(drill.empty());

    std::vector<SourceSlicer::StreamBar> items;
    items.push_back({ 0, {} });
    items.push_back({ -1, drill });
    items.push_back({ 1, {} });
    const std::string window = slicer.window_xml_for(items, 0);
    REQUIRE_FALSE(window.empty());

    const auto onsets = engrave_onsets(window);
    // The drill bar occupies local q [3, 6): bass+chord on beat 1, the
    // repeated grab on beats 2 and 3.
    const auto beat1 = onsets.find(3.0);
    REQUIRE(beat1 != onsets.end());
    CHECK(beat1->second == std::vector<int>{ 52, 60 });
    const auto beat2 = onsets.find(4.0);
    REQUIRE(beat2 != onsets.end());
    CHECK(beat2->second == std::vector<int>{ 60 });
    const auto beat3 = onsets.find(5.0);
    REQUIRE(beat3 != onsets.end());
    CHECK(beat3->second == std::vector<int>{ 60 });
    // And bar 1 of the piece follows at local q 6 (the piece's bar-1 bass).
    CHECK(onsets.lower_bound(6.0) != onsets.end());
}

TEST_CASE("a fresh player gets the piece verbatim", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);

    const int planned = composer.ensure(20);
    REQUIRE(planned >= 20);
    for (int slot = 0; slot < 20; ++slot)
    {
        CHECK(composer.plan(slot).kind == StreamBarPlan::Kind::Piece);
        CHECK(composer.plan(slot).source_bar == slot);
    }
    CHECK(composer.slot_start_q(2) == Catch::Approx(6.0)); // 3/4 bars
}

TEST_CASE("chord trouble earns a drill with a cooldown", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_chord_trouble(model, { 52, 60 }, 5);

    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);
    composer.ensure(30);

    std::vector<int> drill_slots;
    for (int slot = 0; slot < 30; ++slot)
    {
        const StreamBarPlan& plan = composer.plan(slot);
        if (plan.kind == StreamBarPlan::Kind::Drill)
        {
            drill_slots.push_back(slot);
            CHECK(plan.reason.find("52+60") != std::string::npos);
            CHECK_FALSE(plan.drill_xml.empty());
        }
    }
    REQUIRE_FALSE(drill_slots.empty());
    for (size_t i = 1; i < drill_slots.size(); ++i)
        CHECK(drill_slots[i] - drill_slots[i - 1] >= StreamComposer::kDrillCooldownSlots);
    // Piece bars still dominate: never boring.
    int piece_bars = 0;
    for (int slot = 0; slot < 30; ++slot)
        piece_bars += composer.plan(slot).kind == StreamBarPlan::Kind::Piece ? 1 : 0;
    CHECK(piece_bars >= 20);
}

TEST_CASE("weak encountered bars come back as reviews", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_weak_bar(model, 1, 3.0);

    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);
    composer.ensure(40);

    int reviews_of_bar_1 = 0;
    for (int slot = 0; slot < 40; ++slot)
    {
        const StreamBarPlan& plan = composer.plan(slot);
        if (plan.kind == StreamBarPlan::Kind::Review)
        {
            CHECK(plan.source_bar == 1);
            CHECK(plan.reason.find("review bar 2") != std::string::npos);
            ++reviews_of_bar_1;
        }
    }
    CHECK(reviews_of_bar_1 >= 1);
    CHECK(reviews_of_bar_1 <= StreamComposer::kMaxReviewsPerBar);

    // Provenance geometry: the stream axis accounts for every slot, and a
    // review slot maps back to its source bar.
    const int last = composer.planned() - 1;
    CHECK(composer.slot_start_q(last + 1)
        == Catch::Approx(3.0 * static_cast<double>(composer.planned())));
    CHECK(composer.slot_at(composer.slot_start_q(2) + 0.5) == 2);
}

TEST_CASE("drill outcomes train pitch and chord stats but never bar mastery",
    "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    FlowController::NoteOutcome outcome;
    outcome.onset_q = PlayerModel::kDrillOnsetSentinel;
    outcome.pitch = 60;
    outcome.verdict = FlowController::NoteVerdict::Correct;
    outcome.quality = 1.0;
    model.apply(outcome);

    CHECK(model.pitch_stats().at(60).hit == 1);
    CHECK(model.onset_stats().empty());
    for (int bar = 0; bar < 4; ++bar)
        CHECK(model.bar_encounters(bar) == 0);
}

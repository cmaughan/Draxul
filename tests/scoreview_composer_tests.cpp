// Stream milestone S3 (plans/scoreview-stream.md): the composer — program
// planning from the player model (piece / review / drill), fabricated
// chord-drill bars engraved through the same window path, provenance
// geometry, and the drill sentinel in the player model.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/stream_composer.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <cstdlib>
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
    ChordOutcome chord;
    chord.pitches = std::move(pitches);
    chord.result = ChordOutcome::Result::Miss;
    for (int i = 0; i < misses; ++i)
        model.apply(chord);
}

void add_weak_bar(PlayerModel& model, int bar, double quarters_per_bar)
{
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = bar * quarters_per_bar + beat;
        outcome.pitch = 60;
        outcome.verdict = NoteVerdict::Missed;
        model.apply(outcome);
    }
}

// Meet a bar at a chosen recent-encounter quality — the structure tests need
// bars parked BETWEEN the review and seam thresholds.
void add_bar_at_quality(PlayerModel& model, int bar, double quarters_per_bar, double quality)
{
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = bar * quarters_per_bar + beat;
        outcome.pitch = 60 + beat;
        outcome.verdict = quality > 0.0 ? NoteVerdict::Correct : NoteVerdict::Missed;
        outcome.quality = quality;
        model.apply(outcome);
    }
}

void add_hand_pass(PlayerModel& model, int bar, double quarters_per_bar, int staff, bool clean)
{
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = bar * quarters_per_bar + beat;
        outcome.pitch = staff == 2 ? 48 + beat : 64 + beat;
        outcome.staff = staff;
        outcome.verdict = clean ? NoteVerdict::Correct : NoteVerdict::Missed;
        outcome.quality = clean ? 1.0 : 0.0;
        model.apply(outcome);
    }
    model.close_open_pass();
}

// A profile with hand-placed phrase boundaries: the composer only reads
// phrases/bar_positions/structure_confidence, so stating them directly keeps
// the C1/C2 tests about composer policy rather than about detection.
PieceProfile phrased_profile(int total_bars, const std::vector<int>& starts, double confidence)
{
    PieceProfile profile;
    profile.structure_confidence = confidence;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        PieceProfile::Phrase phrase;
        phrase.start_bar = starts[i];
        phrase.end_bar = i + 1 < starts.size() ? starts[i + 1] : total_bars;
        phrase.confidence = confidence;
        profile.phrases.push_back(phrase);
    }
    profile.bar_positions.assign(static_cast<size_t>(total_bars), PieceProfile::BarPosition{});
    for (size_t p = 0; p < profile.phrases.size(); ++p)
    {
        const PieceProfile::Phrase& phrase = profile.phrases[p];
        for (int bar = phrase.start_bar; bar < phrase.end_bar && bar < total_bars; ++bar)
        {
            PieceProfile::BarPosition& pos = profile.bar_positions[static_cast<size_t>(bar)];
            pos.phrase_id = static_cast<int>(p);
            pos.serial_in_phrase = bar - phrase.start_bar;
            pos.phrase_length = phrase.end_bar - phrase.start_bar;
            pos.phrase_open = bar == phrase.start_bar;
        }
    }
    return profile;
}

bool program_has_reason(const StreamProgram& program, const std::string& needle)
{
    for (int slot = 0; slot < program.size(); ++slot)
    {
        if (program.plan(slot).reason.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("a fabricated drill bar engraves inside a composed window", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);

    const std::string drill = composer.fabricate_chord_drill("52+60", 0, /*broken=*/false);
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

TEST_CASE("the stream composer supports single-part sources only", "[scoreview][composer]")
{
    // Source compatibility is the composer's capability (IComposer::supports),
    // not a host rule: the adaptive stream fabricates grand-staff drill bars,
    // so a multi-part piece streams verbatim instead.
    StreamComposer composer;
    CHECK(composer.supports(grieg_slicer()));

    SourceSlicer unloaded;
    CHECK_FALSE(composer.supports(unloaded));

    const std::string two_parts_xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list>
    <score-part id="P1"><part-name>A</part-name></score-part>
    <score-part id="P2"><part-name>B</part-name></score-part>
  </part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>1</divisions>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef></attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch>
        <duration>4</duration><type>whole</type></note>
    </measure>
    <measure number="2">
      <note><pitch><step>D</step><octave>4</octave></pitch>
        <duration>4</duration><type>whole</type></note>
    </measure>
  </part>
  <part id="P2">
    <measure number="1">
      <attributes><divisions>1</divisions>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>F</sign><line>4</line></clef></attributes>
      <note><pitch><step>C</step><octave>3</octave></pitch>
        <duration>4</duration><type>whole</type></note>
    </measure>
    <measure number="2">
      <note><pitch><step>D</step><octave>3</octave></pitch>
        <duration>4</duration><type>whole</type></note>
    </measure>
  </part>
</score-partwise>)";
    SourceSlicer two_parts;
    std::string error;
    REQUIRE(two_parts.load(two_parts_xml, error));
    REQUIRE(two_parts.part_count() == 2);
    CHECK_FALSE(composer.supports(two_parts));
}

TEST_CASE("a fresh player gets the piece verbatim", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);

    StreamProgram program;
    const int planned = composer.ensure(program, 20);
    REQUIRE(planned >= 20);
    for (int slot = 0; slot < 20; ++slot)
    {
        CHECK(program.plan(slot).kind == StreamBarPlan::Kind::Piece);
        CHECK(program.plan(slot).source_bar == slot);
    }
    CHECK(program.slot_start_q(2) == Catch::Approx(6.0)); // 3/4 bars
}

TEST_CASE("chord trouble earns a drill with a cooldown", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_chord_trouble(model, { 52, 60 }, 5);

    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);
    composer.set_drills_enabled(true);
    composer.set_scales_enabled(true);
    StreamProgram program;
    composer.ensure(program, 30);

    std::vector<int> drill_slots;
    for (int slot = 0; slot < 30; ++slot)
    {
        const StreamBarPlan& plan = program.plan(slot);
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
        piece_bars += program.plan(slot).kind == StreamBarPlan::Kind::Piece ? 1 : 0;
    CHECK(piece_bars >= 20);
}

TEST_CASE("weak encountered bars come back as reviews", "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_weak_bar(model, 1, 3.0);

    StreamComposer composer;
    composer.configure(&grieg_slicer(), &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 40);

    int reviews_of_bar_1 = 0;
    for (int slot = 0; slot < 40; ++slot)
    {
        const StreamBarPlan& plan = program.plan(slot);
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
    const int last = program.size() - 1;
    CHECK(program.slot_start_q(last + 1)
        == Catch::Approx(3.0 * static_cast<double>(program.size())));
    CHECK(program.slot_at(program.slot_start_q(2) + 0.5) == 2);
    for (int slot = 0; slot < 40; ++slot)
    {
        const StreamBarPlan& plan = program.plan(slot);
        if (plan.kind != StreamBarPlan::Kind::Review)
            continue;
        // A position inside the review slot trains the SOURCE bar's onsets.
        const auto ref = program.source_at(program.slot_start_q(slot) + 0.5);
        CHECK_FALSE(ref.drill);
        CHECK(ref.source_bar == plan.source_bar);
        CHECK(ref.source_q == Catch::Approx(plan.source_bar * 3.0 + 0.5));
    }
}

TEST_CASE("drill outcomes train pitch and chord stats but never bar mastery",
    "[scoreview][composer]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    NoteOutcome outcome;
    outcome.onset_q = PlayerModel::kDrillOnsetSentinel;
    outcome.pitch = 60;
    outcome.verdict = NoteVerdict::Correct;
    outcome.quality = 1.0;
    model.apply(outcome);

    CHECK(model.pitch_stats().at(60).hit == 1);
    CHECK(model.onset_stats().empty());
    for (int bar = 0; bar < 4; ++bar)
        CHECK(model.bar_encounters(bar) == 0);
}

TEST_CASE("the broken drill arpeggiates before the block grab", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    composer.set_drills_enabled(true);
    composer.set_scales_enabled(true);
    composer.set_drills_enabled(true);
    composer.set_scales_enabled(true);

    // Broken form in 3/4: four eighths cycling the uppers, block on beat 3.
    const std::string broken = composer.fabricate_chord_drill("52+60+64", 0, true);
    REQUIRE_FALSE(broken.empty());
    std::vector<SourceSlicer::StreamBar> items;
    items.push_back({ -1, broken });
    const auto onsets = engrave_onsets(slicer.window_xml_for(items, 0));
    REQUIRE(onsets.size() >= 5);
    const auto beat1 = onsets.find(0.0);
    REQUIRE(beat1 != onsets.end());
    CHECK(beat1->second == std::vector<int>{ 52, 60 }); // bass under the first eighth
    const auto half = onsets.find(0.5);
    REQUIRE(half != onsets.end());
    CHECK(half->second == std::vector<int>{ 64 }); // arpeggio continues
    const auto block = onsets.find(2.0);
    REQUIRE(block != onsets.end());
    CHECK(block->second == std::vector<int>{ 60, 64 }); // the grab lands whole
}

TEST_CASE("hands separate strips one staff and still engraves", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    // Bar 2 of the Grieg has both hands. Keep staff 2 (LH alone).
    const auto staves = slicer.staff_pitches(2);
    REQUIRE(staves.count(1) == 1);
    REQUIRE(staves.count(2) == 1);

    const std::string lh_only = slicer.hands_separate_xml(2, 2);
    REQUIRE_FALSE(lh_only.empty());
    std::vector<SourceSlicer::StreamBar> items;
    items.push_back({ -1, lh_only });
    const auto onsets = engrave_onsets(slicer.window_xml_for(items, 2));
    REQUIRE_FALSE(onsets.empty());
    // Every sounding pitch belongs to the kept staff's pitch set.
    std::vector<int> allowed = staves.at(2);
    std::sort(allowed.begin(), allowed.end());
    for (const auto& [q, pitches] : onsets)
    {
        for (const int pitch : pitches)
            CHECK(std::binary_search(allowed.begin(), allowed.end(), pitch));
    }
}

TEST_CASE("hands separate targets the repeatedly missed hand until it plays cleanly",
    "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_hand_pass(model, 2, 3.0, 1, true);
    add_hand_pass(model, 2, 3.0, 2, false);

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 16);

    int hand_slot = -1;
    for (int slot = 0; slot < program.size(); ++slot)
    {
        if (program.plan(slot).reason.find("left hand alone") != std::string::npos)
        {
            hand_slot = slot;
            break;
        }
    }
    REQUIRE(hand_slot >= 0);
    const StreamBarPlan& hand_plan = program.plan(hand_slot);
    CHECK(hand_plan.kind == StreamBarPlan::Kind::Drill);
    CHECK(hand_plan.drill_trains_source);
    CHECK(hand_plan.source_bar == 2);
    CHECK(hand_plan.drill_xml.find("<staff>2</staff>") != std::string::npos);
    CHECK(hand_plan.drill_xml.find("<staff>1</staff>") == std::string::npos);

    const auto ref = program.source_at(program.slot_start_q(hand_slot) + 0.5);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == 2);
    CHECK(ref.source_q == Catch::Approx(6.5));

    add_hand_pass(model, 2, 3.0, 2, true);
    StreamComposer after_clean;
    after_clean.configure(&slicer, &model, nullptr);
    StreamProgram clean_program;
    after_clean.ensure(clean_program, 16);
    for (int slot = 0; slot < clean_program.size(); ++slot)
        CHECK(clean_program.plan(slot).reason.find("left hand alone") == std::string::npos);
}

TEST_CASE("a scale fragment runs through the troubled register in key",
    "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    PieceProfile profile;
    profile.global_key.tonic_pc = 9; // A
    profile.global_key.minor = true;
    StreamComposer composer;
    composer.configure(&slicer, &model, &profile);
    composer.set_drills_enabled(true);
    composer.set_scales_enabled(true);

    const std::string scale = composer.fabricate_scale_bar(0, 66); // around F#4
    REQUIRE_FALSE(scale.empty());
    std::vector<SourceSlicer::StreamBar> items;
    items.push_back({ -1, scale });
    const auto onsets = engrave_onsets(slicer.window_xml_for(items, 0));
    REQUIRE(onsets.size() == 6); // eighths in 3/4
    // Ascending A natural-minor degrees starting on the tonic below F#4.
    const std::vector<int> expected = { 57, 59, 60, 62, 64, 65 };
    size_t at = 0;
    for (const auto& [q, pitches] : onsets)
    {
        REQUIRE(pitches.size() == 1);
        CHECK(pitches[0] == expected[at]);
        ++at;
    }
}

TEST_CASE("the arc loops weakest slices until mastery earns the performance run",
    "[scoreview][composer]")
{
    // A tiny virtual session against the real Grieg geometry: every bar of
    // the piece is encountered; bars 8..15 stay weak, the rest promote.
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    const int total = slicer.bar_count();
    for (int bar = 0; bar < total; ++bar)
    {
        const bool weak = bar >= 8 && bar < 16;
        for (int beat = 0; beat < 3; ++beat)
        {
            NoteOutcome outcome;
            outcome.onset_q = bar * 3.0 + beat;
            outcome.pitch = 60;
            outcome.verdict = weak ? NoteVerdict::Missed
                                   : NoteVerdict::Correct;
            outcome.quality = weak ? 0.0 : 1.0;
            for (int enc = 0; enc < 3; ++enc)
                model.apply(outcome);
        }
    }

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    // Plan far past the piece: the frontier finishes, then arcs begin.
    StreamProgram program;
    composer.ensure(program, total + 40);
    REQUIRE_FALSE(composer.finished());
    bool arc_hits_weak_region = false;
    for (int slot = total; slot < program.size(); ++slot)
    {
        const StreamBarPlan& plan = program.plan(slot);
        if (plan.kind == StreamBarPlan::Kind::Piece && plan.source_bar >= 8
            && plan.source_bar < 16)
            arc_hits_weak_region = true;
    }
    CHECK(arc_hits_weak_region);

    // Promote everything: promotion now demands CONSECUTIVE CLEAN COMPLETE
    // passes, so sweep the whole piece cleanly three times (a mean of good
    // qualities no longer suffices — that was G5).
    PlayerModel mastered;
    mastered.set_piece("Walz", 130.0, 3.0);
    for (int sweep = 0; sweep < 3; ++sweep)
    {
        for (int bar = 0; bar < total; ++bar)
        {
            for (int beat = 0; beat < 3; ++beat)
            {
                NoteOutcome outcome;
                outcome.onset_q = bar * 3.0 + beat;
                outcome.pitch = 60;
                outcome.verdict = NoteVerdict::Correct;
                outcome.quality = 1.0;
                mastered.apply(outcome);
            }
        }
    }
    mastered.close_open_pass(); // the final bar's traversal counts too
    StreamComposer earned;
    earned.configure(&slicer, &mastered, nullptr);
    StreamProgram earned_program;
    earned.ensure(earned_program, 2 * total + 10);
    REQUIRE(earned.finished());
    // The program: one full pass, then the performance run, then the end.
    CHECK(earned_program.size() == 2 * total);
    bool performance_marked = false;
    for (int slot = total; slot < earned_program.size(); ++slot)
    {
        CHECK(earned_program.plan(slot).kind == StreamBarPlan::Kind::Piece);
        CHECK(earned_program.plan(slot).source_bar == slot - total);
        performance_marked |= earned_program.plan(slot).reason.find("performance run")
            != std::string::npos;
    }
    CHECK(performance_marked);
}

TEST_CASE("keyboard geometry maps 88 keys", "[scoreview][keyboard]")
{
    CHECK(keyboard_white_index(21) == 0); // A0
    CHECK(keyboard_white_index(108) == 51); // C8
    CHECK(keyboard_white_index(22) == -1); // A#0 is black
    CHECK(keyboard_is_black(61));
    CHECK_FALSE(keyboard_is_black(60));
    int whites = 0;
    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
        whites += keyboard_is_black(midi) ? 0 : 1;
    CHECK(whites == kKeyboardWhiteKeys);
    // Middle C sits left of C#4, which sits on the C/D boundary.
    const float c4 = keyboard_key_center_x(60, 0.0f, 520.0f);
    const float cs4 = keyboard_key_center_x(61, 0.0f, 520.0f);
    const float d4 = keyboard_key_center_x(62, 0.0f, 520.0f);
    CHECK(c4 < cs4);
    CHECK(cs4 < d4);
    CHECK(cs4 == Catch::Approx((c4 + d4) * 0.5f).margin(0.01));
}

TEST_CASE("trailing clean plays gate the guidance keyboard", "[scoreview][keyboard]")
{
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    CHECK(model.onset_trailing_correct(3.0) == 0); // never played: show

    const auto play = [&model](double q, bool clean) {
        NoteOutcome outcome;
        outcome.onset_q = q;
        outcome.pitch = 60;
        outcome.verdict = clean ? NoteVerdict::Correct
                                : NoteVerdict::Missed;
        outcome.quality = clean ? 1.0 : 0.0;
        model.apply(outcome);
    };
    play(3.0, true);
    play(3.0, true);
    CHECK(model.onset_trailing_correct(3.0) == 2); // still shows, faded
    play(3.0, true);
    CHECK(model.onset_trailing_correct(3.0) == 3); // invisible now
    play(3.0, false);
    CHECK(model.onset_trailing_correct(3.0) == 0); // a miss brings it back
}

TEST_CASE("the engine recovers enharmonic spelling from the score", "[scoreview][composer]")
{
    // A C#4 and a Db4: the same key and MIDI pitch, opposite spellings. The
    // engine must report their notated letters apart so the palette can color
    // them differently. (Verovio needs the <accidental> element, not just
    // <alter>, to sound the alteration.)
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>P</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>1</divisions><key><fifths>0</fifths></key>
        <time><beats>2</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef></attributes>
      <note><pitch><step>C</step><alter>1</alter><octave>4</octave></pitch>
        <duration>1</duration><type>quarter</type><accidental>sharp</accidental></note>
      <note><pitch><step>D</step><alter>-1</alter><octave>4</octave></pitch>
        <duration>1</duration><type>quarter</type><accidental>flat</accidental></note>
    </measure>
  </part>
</score-partwise>)";
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(xml, error));
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());

    std::vector<int> midis;
    std::vector<int> letters;
    std::vector<int> palette;
    for (const TimemapEntry& entry : timemap->entries)
    {
        for (const std::string& id : entry.note_on)
        {
            const int midi = engine->midi_pitch_for_element(id);
            const int letter = engine->note_letter_for_element(id);
            midis.push_back(midi);
            letters.push_back(letter);
            palette.push_back(guidance_palette_index(midi, letter));
        }
    }
    REQUIRE(midis.size() == 2);
    CHECK(midis[0] == midis[1]); // enharmonic: the same sounding pitch
    std::sort(letters.begin(), letters.end());
    CHECK(letters[0] == 0); // C# is a C
    CHECK(letters[1] == 1); // Db is a D
    // ...and that difference reaches the palette: two colors, not one.
    CHECK(palette[0] != palette[1]);
}

TEST_CASE("the pairing palette colors spellings, not just pitch classes", "[scoreview][keyboard]")
{
    const auto differ = [](const unsigned char* a, const unsigned char* b) {
        return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
    };
    const auto same = [](const unsigned char* a, const unsigned char* b) {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    };

    // An accidental wears its PARENT letter's exact color: C# = C, Db = D. C#
    // and Db are the same key/pitch (61) but different letters, so they still
    // read apart (C's red vs D's orange) even though neither is recolored —
    // the half-moon notehead, not a hue shift, marks them as accidentals.
    const int c_sharp = guidance_palette_index(61, /*letter C=*/0);
    const int d_flat = guidance_palette_index(61, /*letter D=*/1);
    const int c_natural = guidance_palette_index(60, 0);
    const int d_natural = guidance_palette_index(62, 1);
    CHECK(c_sharp != d_flat);
    CHECK(same(kGuidancePalette[c_sharp], kGuidancePalette[c_natural])); // C# = C
    CHECK(same(kGuidancePalette[d_flat], kGuidancePalette[d_natural])); // Db = D
    CHECK(differ(kGuidancePalette[c_sharp], kGuidancePalette[d_flat]) > 80); // C red vs D orange

    // A spelling index is stable across octaves (C#4 and C#6 share it).
    CHECK(guidance_palette_index(61, 0) == guidance_palette_index(85, 0));

    // With no notated letter, the pitch class falls back to its sharp reading.
    CHECK(guidance_palette_index(61) == c_sharp);
    CHECK(guidance_palette_index(60) == c_natural);

    // The seven WHITE KEYS (naturals) must all be clearly distinct — the
    // point of the redesign: C and F used to look alike, now they don't.
    const int naturals[7] = {
        guidance_palette_index(60, 0), // C
        guidance_palette_index(62, 1), // D
        guidance_palette_index(64, 2), // E
        guidance_palette_index(65, 3), // F
        guidance_palette_index(67, 4), // G
        guidance_palette_index(69, 5), // A
        guidance_palette_index(71, 6), // B
    };
    int min_natural = 1000;
    for (int i = 0; i < 7; ++i)
        for (int j = i + 1; j < 7; ++j)
            min_natural = std::min(min_natural,
                differ(kGuidancePalette[naturals[i]], kGuidancePalette[naturals[j]]));
    CHECK(min_natural > 55);
    CHECK(differ(kGuidancePalette[naturals[0]], kGuidancePalette[naturals[3]]) > 200); // C vs F

    // Every pitch maps in range. (Adjacent semitones may share a color now —
    // a natural and its sharp are the same hue, told apart by the half-moon
    // notehead, not the color — so there is no chromatic-contrast check.)
    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
    {
        const int idx = guidance_palette_index(midi);
        REQUIRE(idx >= 0);
        REQUIRE(idx < kGuidancePaletteSize);
    }
}

// --- Structure-aware chunking (C1/C2, plans/scoreview-composer.md) --------
// The evidence: practice segments belong on phrase boundaries, and recall
// collapses from a phrase's opening bar toward its tail. Fixed-length slices
// are the fallback for material whose structure we cannot honestly see.

TEST_CASE("arcs slice on the weakest phrase when structure is confident",
    "[scoreview][composer][structure]")
{
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    const double qpb = slicer.bar_quarters(0);
    REQUIRE(total >= 8);

    const std::vector<int> starts = { 0, total / 4, total / 2, (3 * total) / 4 };
    const PieceProfile profile = phrased_profile(total, starts, 0.9);
    const int weak_first = starts[2];
    const int weak_last = starts[3];

    // Everything solid except the third phrase: nothing has promoted the
    // piece, so the arc must aim at that phrase's exact bar range.
    PlayerModel model;
    model.set_piece("grieg", 120.0, qpb);
    for (int bar = 0; bar < total; ++bar)
    {
        if (bar >= weak_first && bar < weak_last)
            add_weak_bar(model, bar, qpb);
        else
            add_bar_at_quality(model, bar, qpb, 1.0);
    }

    StreamComposer composer;
    composer.configure(&slicer, &model, &profile);
    StreamProgram program;
    composer.ensure(program, total * 3);

    CHECK(program_has_reason(program,
        "weakest phrase, bars " + std::to_string(weak_first + 1) + ".."
            + std::to_string(weak_last)));
    CHECK_FALSE(program_has_reason(program, "weakest slice"));
}

TEST_CASE("arcs fall back to fixed slices without confident structure",
    "[scoreview][composer][structure]")
{
    // Unphrased/atonal material: the composer must not trust invented
    // structure, so the fixed window survives exactly here.
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    const double qpb = slicer.bar_quarters(0);
    PlayerModel model;
    model.set_piece("grieg", 120.0, qpb);
    for (int bar = 0; bar < total; ++bar)
        add_weak_bar(model, bar, qpb);

    const PieceProfile guessed = phrased_profile(total, { 0, total / 2 }, 0.1);

    SECTION("confidence below the gate")
    {
        StreamComposer composer;
        composer.configure(&slicer, &model, &guessed);
        StreamProgram program;
        composer.ensure(program, total * 3);
        CHECK(program_has_reason(program, "weakest slice"));
        CHECK_FALSE(program_has_reason(program, "weakest phrase"));
    }

    SECTION("no profile at all")
    {
        StreamComposer composer;
        composer.configure(&slicer, &model, nullptr);
        StreamProgram program;
        composer.ensure(program, total * 3);
        CHECK(program_has_reason(program, "weakest slice"));
    }
}

TEST_CASE("review prefers the phrase tail over its opening at equal mastery",
    "[scoreview][composer][structure]")
{
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    const double qpb = slicer.bar_quarters(0);
    PlayerModel model;
    model.set_piece("grieg", 120.0, qpb);

    // Bars 0 and 3 are equally weak, but bar 0 opens the phrase (a cheap
    // anchor) and bar 3 ends it (where recall collapses).
    const PieceProfile profile = phrased_profile(total, { 0, 4 }, 0.9);
    add_weak_bar(model, 0, qpb);
    add_weak_bar(model, 3, qpb);

    StreamComposer composer;
    composer.configure(&slicer, &model, &profile);
    StreamProgram program;
    composer.ensure(program, 16);

    int first_review_bar = -1;
    for (int slot = 0; slot < program.size() && first_review_bar < 0; ++slot)
    {
        if (program.plan(slot).kind == StreamBarPlan::Kind::Review)
            first_review_bar = program.plan(slot).source_bar;
    }
    CHECK(first_review_bar == 3);
}

TEST_CASE("the seam special practises the join the arc never crosses",
    "[scoreview][composer][structure]")
{
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    const double qpb = slicer.bar_quarters(0);
    PlayerModel model;
    model.set_piece("grieg", 120.0, qpb);

    // Bar 3 ends phrase 0 and bar 4 opens phrase 1. Both are individually
    // above the review threshold, so only the seam can catch the join — which
    // is the one place arc practice (one phrase at a time) never reaches.
    const PieceProfile profile = phrased_profile(total, { 0, 4 }, 0.9);
    add_bar_at_quality(model, 3, qpb, 0.55);
    add_bar_at_quality(model, 4, qpb, 0.55);

    StreamComposer composer;
    composer.configure(&slicer, &model, &profile);
    StreamProgram program;
    composer.ensure(program, 24);

    int seam_slot = -1;
    for (int slot = 0; slot + 1 < program.size() && seam_slot < 0; ++slot)
    {
        if (program.plan(slot).reason.find("seam 4->5") != std::string::npos)
            seam_slot = slot;
    }
    REQUIRE(seam_slot >= 0);
    // The pair is served back-to-back, in order, as real source bars.
    CHECK(program.plan(seam_slot).kind == StreamBarPlan::Kind::Review);
    CHECK(program.plan(seam_slot).source_bar == 3);
    CHECK(program.plan(seam_slot + 1).kind == StreamBarPlan::Kind::Review);
    CHECK(program.plan(seam_slot + 1).source_bar == 4);
}

TEST_CASE("an unmet join is the frontier's job, not the seam's",
    "[scoreview][composer][structure]")
{
    // Bar 4 has never been played: the seam repairs joins, it does not
    // introduce material.
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    const double qpb = slicer.bar_quarters(0);
    PlayerModel model;
    model.set_piece("grieg", 120.0, qpb);
    const PieceProfile profile = phrased_profile(total, { 0, 4 }, 0.9);
    add_bar_at_quality(model, 3, qpb, 0.55);

    StreamComposer composer;
    composer.configure(&slicer, &model, &profile);
    StreamProgram program;
    composer.ensure(program, 16);
    CHECK_FALSE(program_has_reason(program, "seam"));
}

// --- C3: clean-complete promotion + error re-serve --------------------------

TEST_CASE("a chronically fumbled bar never promotes on its mean",
    "[scoreview][composer]")
{
    // G5 regression: 70% quality forever used to promote. Three sweeps where
    // bar 5 always fumbles one note: every OTHER bar reaches three clean
    // passes, bar 5 has none, and the performance run must not be earned.
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    for (int sweep = 0; sweep < 3; ++sweep)
    {
        for (int bar = 0; bar < total; ++bar)
        {
            for (int beat = 0; beat < 3; ++beat)
            {
                NoteOutcome outcome;
                outcome.onset_q = bar * 3.0 + beat;
                outcome.pitch = 60;
                const bool fumble = bar == 5 && beat == 1;
                outcome.verdict = fumble ? NoteVerdict::Missed : NoteVerdict::Correct;
                outcome.quality = fumble ? 0.0 : 1.0;
                model.apply(outcome);
            }
        }
    }
    model.close_open_pass();
    CHECK(model.bar_mastery(5) > 0.6); // the mean still looks fine — that was the bug
    CHECK(model.bar_consecutive_clean(5) == 0);

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 3 * total);
    CHECK_FALSE(composer.finished()); // no earned performance run
    bool performance = false;
    for (int slot = 0; slot < program.size(); ++slot)
        performance |= program.plan(slot).reason.find("performance run") != std::string::npos;
    CHECK_FALSE(performance);
}

TEST_CASE("a live fumble earns exactly one immediate re-serve",
    "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 4);

    // Play bar 1 cleanly, fumble bar 2, move into bar 3 (closing bar 2's
    // pass) — mid-program, exactly as the judge feeds the model.
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = beat;
        outcome.pitch = 60;
        outcome.verdict = NoteVerdict::Correct;
        outcome.quality = 1.0;
        model.apply(outcome);
    }
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = 3.0 + beat;
        outcome.pitch = 60;
        outcome.verdict = beat == 0 ? NoteVerdict::Missed : NoteVerdict::Correct;
        outcome.quality = beat == 0 ? 0.0 : 1.0;
        model.apply(outcome);
    }
    NoteOutcome next_bar;
    next_bar.onset_q = 6.0;
    next_bar.pitch = 60;
    next_bar.verdict = NoteVerdict::Correct;
    next_bar.quality = 1.0;
    model.apply(next_bar);

    const int before = program.size();
    composer.ensure(program, before + 6);
    // The very next planned slot is the fix, ahead of the special rotation.
    REQUIRE(program.size() > before);
    const StreamBarPlan& fix = program.plan(before);
    CHECK(fix.kind == StreamBarPlan::Kind::Review);
    CHECK(fix.source_bar == 1);
    CHECK(fix.reason.find("fix") != std::string::npos);
    // One fumble, one fix: no second re-serve for the same pass.
    int fixes = 0;
    for (int slot = 0; slot < program.size(); ++slot)
        fixes += program.plan(slot).reason.find("fix") != std::string::npos ? 1 : 0;
    CHECK(fixes == 1);
}

TEST_CASE("restored history never front-loads fixes", "[scoreview][composer]")
{
    // A model carrying yesterday's fumbles: re-serve responds to LIVE
    // errors, so a fresh program must not open with corrections.
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_weak_bar(model, 2, 3.0);
    NoteOutcome closer;
    closer.onset_q = 9.0;
    closer.pitch = 60;
    closer.verdict = NoteVerdict::Correct;
    closer.quality = 1.0;
    model.apply(closer);
    model.close_open_pass();
    REQUIRE(model.bar_last_pass_dirty(2));

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 6);
    for (int slot = 0; slot < program.size(); ++slot)
        CHECK(program.plan(slot).reason.find("fix") == std::string::npos);
}

// --- C4: the session opening (overnight re-tests + spaced reviews) ----------

TEST_CASE("the session opens with overnight re-tests, then due spaced reviews",
    "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    const auto pass_bar = [&](int bar, bool clean) {
        for (int beat = 0; beat < 3; ++beat)
        {
            NoteOutcome outcome;
            outcome.onset_q = bar * 3.0 + beat;
            outcome.pitch = 60;
            const bool fumble = !clean && beat == 1;
            outcome.verdict = fumble ? NoteVerdict::Missed : NoteVerdict::Correct;
            outcome.quality = fumble ? 0.0 : 1.0;
            model.apply(outcome);
        }
        model.close_open_pass();
    };
    // Three days ago: bar 5 clean. Yesterday: bar 2 fumbled. Today: bar 7
    // clean already — not due again.
    model.begin_session("2026-07-14T09:00:00");
    pass_bar(5, true);
    model.end_session(60, 0.6);
    model.begin_session("2026-07-16T09:00:00");
    pass_bar(2, false);
    model.end_session(60, 0.6);
    model.begin_session("2026-07-17T09:00:00");
    pass_bar(7, true);

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 8);

    // Slot 0: the overnight re-test of yesterday's problem point — sleep
    // consolidates the hardest transitions, so it is re-tested, not drilled.
    REQUIRE(program.size() >= 2);
    CHECK(program.plan(0).kind == StreamBarPlan::Kind::Review);
    CHECK(program.plan(0).source_bar == 2);
    CHECK(program.plan(0).reason.find("overnight re-test") != std::string::npos);
    // Slot 1: the spaced review of the three-day-old clean bar.
    CHECK(program.plan(1).kind == StreamBarPlan::Kind::Review);
    CHECK(program.plan(1).source_bar == 5);
    CHECK(program.plan(1).reason.find("spaced review") != std::string::npos);
    // Bar 7 (seen today) appears in no opening slot.
    for (int slot = 0; slot < program.size(); ++slot)
    {
        if (program.plan(slot).reason.find("re-test") != std::string::npos
            || program.plan(slot).reason.find("spaced") != std::string::npos)
            CHECK(program.plan(slot).source_bar != 7);
    }
}

TEST_CASE("a session with no history opens straight into the frontier",
    "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    model.begin_session("2026-07-17T09:00:00");
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 4);
    REQUIRE(program.size() >= 1);
    CHECK(program.plan(0).kind == StreamBarPlan::Kind::Piece);
    CHECK(program.plan(0).source_bar == 0);
}

// --- The rewriting composer: urgent splice -----------------------------------

TEST_CASE("a fumble splices its fix just past the playhead", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 10);
    const int before = program.size();

    // Fumble bar 1 mid-play (bar transition closes the pass).
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = 3.0 + beat;
        outcome.pitch = 60;
        outcome.verdict = beat == 1 ? NoteVerdict::Missed : NoteVerdict::Correct;
        outcome.quality = beat == 1 ? 0.0 : 1.0;
        model.apply(outcome);
    }
    NoteOutcome closer;
    closer.onset_q = 6.0;
    closer.pitch = 60;
    closer.verdict = NoteVerdict::Correct;
    closer.quality = 1.0;
    model.apply(closer);

    // Playhead inside slot 2 -> splice at slot 4 (one guard bar).
    const int inserted = composer.plan_urgent(program, 4);
    REQUIRE(inserted == 1);
    REQUIRE(program.size() == before + 1);
    CHECK(program.plan(4).kind == StreamBarPlan::Kind::Review);
    CHECK(program.plan(4).source_bar == 1);
    CHECK(program.plan(4).reason.find("fix now") != std::string::npos);
    // Slots after the splice shifted intact, in order.
    CHECK(program.plan(5).kind == StreamBarPlan::Kind::Piece);

    // The splice CLAIMED the fumble: neither path fixes it twice.
    CHECK(composer.plan_urgent(program, 6) == 0);
    composer.ensure(program, program.size() + 6);
    int fixes = 0;
    for (int slot = 0; slot < program.size(); ++slot)
        fixes += program.plan(slot).reason.find("fix") != std::string::npos ? 1 : 0;
    CHECK(fixes == 1);
}

TEST_CASE("a repeated one-hand fumble splices that hand alone", "[scoreview][composer]")
{
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 10);
    const int before = program.size();

    add_hand_pass(model, 2, 3.0, 2, false);

    const int inserted = composer.plan_urgent(program, 4);
    REQUIRE(inserted == 1);
    REQUIRE(program.size() == before + 1);
    const StreamBarPlan& fix = program.plan(4);
    CHECK(fix.kind == StreamBarPlan::Kind::Drill);
    CHECK(fix.drill_trains_source);
    CHECK(fix.source_bar == 2);
    CHECK(fix.reason.find("fix now") != std::string::npos);
    CHECK(fix.reason.find("left hand alone") != std::string::npos);
    CHECK(fix.drill_xml.find("<staff>2</staff>") != std::string::npos);
    CHECK(fix.drill_xml.find("<staff>1</staff>") == std::string::npos);

    const auto ref = program.source_at(program.slot_start_q(4) + 0.5);
    CHECK_FALSE(ref.drill);
    CHECK(ref.source_bar == 2);
    CHECK(ref.source_q == Catch::Approx(6.5));
}

TEST_CASE("the urgent splice refuses the performance run", "[scoreview][composer]")
{
    // Once every bar has earned the performance, a stray fumble must not
    // tear the run apart mid-flight; the append path handles it afterwards.
    SourceSlicer& slicer = grieg_slicer();
    const int total = slicer.bar_count();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    for (int sweep = 0; sweep < 3; ++sweep)
    {
        for (int bar = 0; bar < total; ++bar)
        {
            for (int beat = 0; beat < 3; ++beat)
            {
                NoteOutcome outcome;
                outcome.onset_q = bar * 3.0 + beat;
                outcome.pitch = 60;
                outcome.verdict = NoteVerdict::Correct;
                outcome.quality = 1.0;
                model.apply(outcome);
            }
        }
    }
    model.close_open_pass();
    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, total + 5); // into the performance run

    // A fumble arrives mid-performance.
    for (int beat = 0; beat < 3; ++beat)
    {
        NoteOutcome outcome;
        outcome.onset_q = beat;
        outcome.pitch = 60;
        outcome.verdict = NoteVerdict::Missed;
        model.apply(outcome);
    }
    NoteOutcome closer;
    closer.onset_q = 3.0;
    closer.pitch = 60;
    closer.verdict = NoteVerdict::Correct;
    closer.quality = 1.0;
    model.apply(closer);
    CHECK(composer.plan_urgent(program, total + 2) == 0);
}

TEST_CASE("drills and scales are opt-in: off by default", "[scoreview][composer]")
{
    // Chord AND register trouble that would previously earn a drill and a
    // scale fragment: with the defaults, neither appears — reviews and the
    // rest of the program are untouched.
    SourceSlicer& slicer = grieg_slicer();
    PlayerModel model;
    model.set_piece("Walz", 130.0, 3.0);
    add_chord_trouble(model, { 52, 60 }, 12);
    for (int i = 0; i < 12; ++i)
    {
        NoteOutcome miss;
        miss.onset_q = PlayerModel::kDrillOnsetSentinel;
        miss.pitch = 30 + (i % 3); // pile misses into one low octave window
        miss.verdict = NoteVerdict::Missed;
        model.apply(miss);
    }
    add_weak_bar(model, 3, 3.0);

    StreamComposer composer;
    composer.configure(&slicer, &model, nullptr);
    StreamProgram program;
    composer.ensure(program, 24);
    bool any_drill = false;
    bool any_scale = false;
    bool any_review = false;
    for (int slot = 0; slot < program.size(); ++slot)
    {
        const std::string& reason = program.plan(slot).reason;
        any_drill |= reason.find("drill") != std::string::npos;
        any_scale |= reason.find("scale") != std::string::npos;
        any_review |= program.plan(slot).kind == StreamBarPlan::Kind::Review;
    }
    CHECK_FALSE(any_drill);
    CHECK_FALSE(any_scale);
    CHECK(any_review); // the rest of the pedagogy still runs

    // Opting in brings them back for future planning.
    composer.set_drills_enabled(true);
    composer.set_scales_enabled(true);
    composer.ensure(program, program.size() + 16);
    bool drill_after = false;
    for (int slot = 0; slot < program.size(); ++slot)
        drill_after |= program.plan(slot).reason.find("drill") != std::string::npos;
    CHECK(drill_after);
}

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

    // Promote everything: the next arc is the performance run, then done.
    PlayerModel mastered;
    mastered.set_piece("Walz", 130.0, 3.0);
    for (int bar = 0; bar < total; ++bar)
    {
        for (int beat = 0; beat < 3; ++beat)
        {
            NoteOutcome outcome;
            outcome.onset_q = bar * 3.0 + beat;
            outcome.pitch = 60;
            outcome.verdict = NoteVerdict::Correct;
            outcome.quality = 1.0;
            for (int enc = 0; enc < 3; ++enc)
                mastered.apply(outcome);
        }
    }
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

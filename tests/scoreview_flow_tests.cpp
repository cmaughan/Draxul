// Conveyor milestone C0-C2 (plans/scoreview-conveyor.md): flow layout mode,
// timemap parsing, the FlowController transport/geometry, and the highlight
// overlay — plus the live Grieg id-join acceptance tests.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace draxul::scoreview;

namespace
{

Timemap parse_ok(const std::string& json)
{
    std::string error;
    auto timemap = parse_timemap(json, error);
    INFO(error);
    REQUIRE(timemap.has_value());
    return std::move(*timemap);
}

// A strip with one glyph per note id at a known x, plus an id-less staff line.
ScoreDrawList make_strip(std::initializer_list<std::pair<const char*, float>> notes)
{
    ScoreDrawList strip;
    strip.canvas_size = { 10000.0f, 500.0f };
    for (const auto& [id, x] : notes)
    {
        GlyphInstance glyph;
        glyph.symbol_index = 0;
        glyph.xform = Affine::translate(x, 100.0f);
        glyph.element_id = id;
        strip.glyphs.push_back(glyph);
    }
    DrawPath staff_line;
    staff_line.cmds.push_back({ PathCmd::Op::MoveTo, { 0.0f, 50.0f } });
    staff_line.cmds.push_back({ PathCmd::Op::LineTo, { 10000.0f, 50.0f } });
    staff_line.stroke_width = 10.0f;
    strip.paths.push_back(staff_line);
    return strip;
}

const char* SIMPLE_TIMEMAP = R"([
  {"qstamp": 0, "tempo": 100, "on": ["n1"], "tstamp": 0},
  {"qstamp": 1, "on": ["n2", "n3"], "off": ["n1"]},
  {"qstamp": 3, "on": ["n4"]},
  {"qstamp": 4, "off": ["n4"]}
])";

} // namespace

TEST_CASE("timemap parser reads entries, tempo, and duration", "[scoreview][flow]")
{
    const Timemap timemap = parse_ok(SIMPLE_TIMEMAP);
    REQUIRE(timemap.entries.size() == 4);
    CHECK(timemap.tempo_qpm == 100.0);
    CHECK(timemap.duration_q == 4.0);
    CHECK(timemap.entries[0].note_on == std::vector<std::string>{ "n1" });
    CHECK(timemap.entries[1].note_on == std::vector<std::string>{ "n2", "n3" });
    CHECK(timemap.entries[1].note_off == std::vector<std::string>{ "n1" });
    CHECK(timemap.entries[3].note_on.empty());
}

TEST_CASE("timemap parser tolerates junk and rejects invalid JSON", "[scoreview][flow]")
{
    // Entries without a qstamp (or non-objects) are skipped; unknown keys ignored.
    const Timemap timemap = parse_ok(
        R"([{"on": ["ghost"]}, 42, {"qstamp": 2, "on": ["n1"], "mystery": {}}])");
    REQUIRE(timemap.entries.size() == 1);
    CHECK(timemap.entries[0].qstamp == 2.0);
    CHECK(timemap.tempo_qpm == 0.0);

    std::string error;
    CHECK_FALSE(parse_timemap("not json at all", error).has_value());
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(parse_timemap(R"({"qstamp": 1})", error).has_value());
    CHECK(error.find("array") != std::string::npos);
}

TEST_CASE("flow controller joins onsets and interpolates x", "[scoreview][flow]")
{
    const Timemap timemap = parse_ok(SIMPLE_TIMEMAP);
    const ScoreDrawList strip = make_strip({ { "n1", 100.0f }, { "n2", 400.0f }, { "n3", 500.0f }, { "n4", 900.0f } });

    FlowController flow;
    std::string error;
    REQUIRE(flow.build(timemap, strip, error));
    CHECK(flow.join_miss_count() == 0);
    CHECK(flow.non_monotonic_count() == 0);
    REQUIRE(flow.onsets().size() == 3); // qstamp 4 is off-only
    CHECK(flow.onsets()[1].x == 400.0f); // chord n2+n3 -> min x
    CHECK(flow.marking_qpm() == 100.0);
    CHECK(flow.duration_q() == 4.0);

    // x_at: clamped before/after, linear between.
    CHECK(flow.x_at(-1.0) == 100.0);
    CHECK(flow.x_at(0.0) == 100.0);
    CHECK(flow.x_at(0.5) == Catch::Approx(250.0));
    CHECK(flow.x_at(2.0) == Catch::Approx(650.0)); // halfway from q1 to q3
    CHECK(flow.x_at(3.0) == 900.0);
    CHECK(flow.x_at(99.0) == 900.0);

    // scroll_x anchors the playhead and clamps to the strip.
    flow.seek(0.0);
    CHECK(flow.scroll_x(2000.0, 0.3) == 0.0); // 100 - 600 clamps to 0
    flow.seek(3.0);
    CHECK(flow.scroll_x(2000.0, 0.3) == Catch::Approx(300.0));
    CHECK(flow.scroll_x(20000.0, 0.3) == 0.0); // viewport wider than strip
}

TEST_CASE("flow controller transport, tempo clamps, and lit diffs", "[scoreview][flow]")
{
    const Timemap timemap = parse_ok(SIMPLE_TIMEMAP);
    const ScoreDrawList strip = make_strip({ { "n1", 100.0f }, { "n2", 400.0f }, { "n3", 500.0f }, { "n4", 900.0f } });
    FlowController flow;
    std::string error;
    REQUIRE(flow.build(timemap, strip, error));

    // Tempo starts at the slow default and clamps to the marking-derived band.
    CHECK(flow.tempo_qpm() == Catch::Approx(100.0 * FlowController::kStartTempoFrac));
    flow.set_tempo_qpm(1000.0);
    CHECK(flow.tempo_qpm() == Catch::Approx(100.0 * FlowController::kMaxTempoFrac));
    flow.set_tempo_qpm(1.0);
    CHECK(flow.tempo_qpm() == Catch::Approx(100.0 * FlowController::kMinTempoFrac));

    // Paused advance is a no-op; playing advances by dt * qpm / 60.
    flow.set_tempo_qpm(60.0); // 1 quarter per second
    flow.advance(1.0);
    CHECK(flow.position_q() == 0.0);
    flow.play();
    REQUIRE(flow.playing());
    flow.advance(1.5);
    CHECK(flow.position_q() == Catch::Approx(1.5));

    // Lit diffs: onsets at q0 and q1 crossed, each id exactly once.
    auto update = flow.take_lit_update();
    CHECK_FALSE(update.reset);
    CHECK(update.newly_lit == std::vector<std::string>{ "n1", "n2", "n3" });
    update = flow.take_lit_update();
    CHECK(update.newly_lit.empty());

    // Reaching the end auto-pauses and lights the rest.
    flow.advance(100.0);
    CHECK(flow.position_q() == 4.0);
    CHECK(flow.at_end());
    CHECK_FALSE(flow.playing());
    update = flow.take_lit_update();
    CHECK(update.newly_lit == std::vector<std::string>{ "n4" });

    // Rewind requests a reset and replays from the start.
    flow.rewind();
    CHECK(flow.position_q() == 0.0);
    update = flow.take_lit_update();
    CHECK(update.reset);
    CHECK(update.newly_lit == std::vector<std::string>{ "n1" });
}

TEST_CASE("flow controller clamps non-monotonic onset x to forward motion", "[scoreview][flow]")
{
    // n2 sits LEFT of n1 (a repeat jumping back); its x clamps forward.
    const Timemap timemap = parse_ok(R"([{"qstamp": 0, "on": ["n1"]}, {"qstamp": 2, "on": ["n2"]}])");
    const ScoreDrawList strip = make_strip({ { "n1", 800.0f }, { "n2", 200.0f } });
    FlowController flow;
    std::string error;
    REQUIRE(flow.build(timemap, strip, error));
    CHECK(flow.non_monotonic_count() == 1);
    CHECK(flow.onsets()[1].x == 800.0f);
}

TEST_CASE("highlight state buckets ops by element id", "[scoreview][flow]")
{
    ScoreDrawList list = make_strip({ { "n1", 100.0f }, { "n2", 400.0f } });
    // Give n1 a second op (its stem) and leave the staff line id-less.
    DrawPath stem;
    stem.cmds.push_back({ PathCmd::Op::MoveTo, { 110.0f, 80.0f } });
    stem.cmds.push_back({ PathCmd::Op::LineTo, { 110.0f, 160.0f } });
    stem.stroke_width = 8.0f;
    stem.element_id = "n1";
    list.paths.push_back(stem);

    ScoreHighlightState highlight;
    highlight.build(list);
    CHECK(highlight.bucket_count() == 2); // n1, n2 — id-less ops excluded
    REQUIRE(highlight.ops_for("n1") != nullptr);
    CHECK(highlight.ops_for("n1")->size() == 2);
    CHECK(highlight.ops_for("missing") == nullptr);

    CHECK(highlight.set_lit("n1"));
    CHECK(highlight.glyph_lit[0] == 1);
    CHECK(highlight.glyph_lit[1] == 0);
    CHECK(highlight.path_lit[0] == 0); // staff line untouched
    CHECK(highlight.path_lit[1] == 1); // the stem
    CHECK_FALSE(highlight.set_lit("missing"));

    highlight.clear_lit();
    CHECK(highlight.glyph_lit[0] == 0);
    CHECK(highlight.path_lit[1] == 0);
}

namespace
{

// A treble bar of four quarters where the third carries a flat: C4 D4 Db4 E4.
// The flat sign engraves LEFT of the Db notehead — the probe for the playhead
// anchor (the accidental must not drag the onset x off the notehead).
constexpr const char* ACCIDENTAL_SCORE = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
      </attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note>
      <note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note>
      <note><pitch><step>D</step><alter>-1</alter><octave>4</octave></pitch><duration>1</duration><type>quarter</type><accidental>flat</accidental></note>
      <note><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note>
    </measure>
  </part>
</score-partwise>)";

// Leading hex of a SymbolOutline id ("E0A2-...") is the SMuFL codepoint; the
// Noteheads block is U+E0A0..E0FF (same predicate the renderer uses).
bool symbol_is_notehead(const std::string& symbol_id)
{
    const long cp = std::strtol(symbol_id.c_str(), nullptr, 16);
    return cp >= 0xE0A0 && cp <= 0xE0FF;
}

} // namespace

TEST_CASE("playhead onset anchor sits on the notehead, not the accidental", "[scoreview][flow]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(ACCIDENTAL_SCORE, error));

    auto strip = interpret_score_svg(engine->render_page_svg(1), error);
    REQUIRE(strip.has_value());
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    FlowController flow;
    REQUIRE(flow.build(*timemap, *strip, error));
    REQUIRE(flow.onsets().size() == 4);

    // For each onset, the anchor must match the leftmost NOTEHEAD glyph of its
    // ids — any sign/stem/flag op sharing the id must not drag it left.
    for (const FlowController::Onset& onset : flow.onsets())
    {
        float head_x = std::numeric_limits<float>::max();
        for (const std::string& id : onset.ids)
        {
            for (const GlyphInstance& glyph : strip->glyphs)
            {
                if (glyph.element_id != id)
                    continue;
                UNSCOPED_INFO("q=" << onset.qstamp << " id=" << id << " glyph symbol="
                                   << strip->symbols[static_cast<size_t>(glyph.symbol_index)].id
                                   << " x=" << glyph.xform.e);
                if (symbol_is_notehead(strip->symbols[static_cast<size_t>(glyph.symbol_index)].id))
                    head_x = std::min(head_x, glyph.xform.e);
            }
        }
        REQUIRE(head_x < std::numeric_limits<float>::max());
        INFO("q=" << onset.qstamp << " anchor=" << onset.x << " notehead=" << head_x);
        CHECK(onset.x == Catch::Approx(head_x).margin(1.0));
    }
}

namespace
{

// The Grieg's ornament figure in miniature: a slashed acciaccatura into an
// accidental note. Verovio timestamps the grace ON the beat and the principal
// a sliver (~1/16 quarter) later, engraved a wide gap apart.
constexpr const char* GRACE_SCORE = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths></key>
        <time><beats>3</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
      </attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note>
      <note><grace slash="yes"/><pitch><step>A</step><octave>4</octave></pitch><type>eighth</type></note>
      <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>1</duration><type>quarter</type><accidental>sharp</accidental></note>
      <note><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note>
    </measure>
  </part>
</score-partwise>)";

} // namespace

TEST_CASE("grace clusters fold into their beat so the playhead never leaps", "[scoreview][flow]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(GRACE_SCORE, error));

    auto strip = interpret_score_svg(engine->render_page_svg(1), error);
    REQUIRE(strip.has_value());
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    FlowController flow;
    REQUIRE(flow.build(*timemap, *strip, error));

    // C4 | grace+F#4 folded | E4 — three anchors, not four. (Verovio's grace
    // placement varies — it steals from the previous note here, but sits ON
    // the beat at a bar start — so the cluster head lands near the beat, not
    // exactly on it; the fold keeps the FIRST onset of the cluster.)
    REQUIRE(flow.onsets().size() == 3);
    CHECK(flow.onsets()[1].qstamp > 0.85);
    CHECK(flow.onsets()[1].qstamp < 1.0 + 1e-9);
    CHECK(flow.onsets()[1].ids.size() == 2); // grace and principal judge together

    // No anchor pair closer than the fold threshold; x strictly forward.
    for (size_t i = 1; i < flow.onsets().size(); ++i)
    {
        CHECK(flow.onsets()[i].qstamp - flow.onsets()[i - 1].qstamp
            >= FlowController::kGraceFoldQ);
        CHECK(flow.onsets()[i].x > flow.onsets()[i - 1].x);
    }
}

namespace
{

// max/median scroll speed (canvas px per beat) of the Grieg strip engraved
// with the given spacing — the flatness measure for the proportional-spacing
// experiment.
double grieg_speed_ratio(bool proportional)
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    options.proportional_spacing = proportional;
    engine->set_options(options);

    std::ifstream stream(
        std::string(DRAXUL_PROJECT_ROOT) + "/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl",
        std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    REQUIRE(engine->load(buffer.str(), error));

    auto strip = interpret_score_svg(engine->render_page_svg(1), error);
    REQUIRE(strip.has_value());
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    FlowController flow;
    REQUIRE(flow.build(*timemap, *strip, error));

    std::vector<double> speeds;
    for (size_t i = 1; i < flow.onsets().size(); ++i)
    {
        const double dq = flow.onsets()[i].qstamp - flow.onsets()[i - 1].qstamp;
        speeds.push_back((flow.onsets()[i].x - flow.onsets()[i - 1].x) / dq);
    }
    std::sort(speeds.begin(), speeds.end());
    return speeds.back() / speeds[speeds.size() / 2];
}

} // namespace

TEST_CASE("proportional spacing flattens the conveyor scroll speed", "[scoreview][flow]")
{
    const double authentic = grieg_speed_ratio(false);
    const double proportional = grieg_speed_ratio(true);
    INFO("max/median speed — authentic: " << authentic << ", proportional: " << proportional);
    // The engraver's 0.6 curve leaves real spread; proportional spacing must
    // be materially flatter (content minimums keep it from perfect 1.0).
    CHECK(proportional < authentic * 0.75);
    CHECK(proportional < 2.0);
}

TEST_CASE("flow layout and timemap join the live Grieg end-to-end", "[scoreview][flow]")
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
    REQUIRE(stream.good());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    REQUIRE(engine->load(buffer.str(), error));

    // One endless system in the known dialect.
    CHECK(engine->page_count() == 1);
    auto strip = interpret_score_svg(engine->render_page_svg(1), error);
    INFO(error);
    REQUIRE(strip.has_value());
    for (const std::string& warning : strip->warnings)
        UNSCOPED_INFO(warning);
    CHECK(strip->warnings.empty());
    CHECK(strip->canvas_size.x > 20.0f * strip->canvas_size.y); // a strip, not a page

    // Timemap ground truth (Verovio 6.2.1 + this fixture, measured 2026-07-11).
    auto timemap = parse_timemap(engine->render_timemap(), error);
    INFO(error);
    REQUIRE(timemap.has_value());
    CHECK(timemap->entries.size() == 308);
    CHECK(timemap->tempo_qpm == 130.0);
    CHECK(timemap->duration_q == 237.0); // 79 measures x 3 quarters

    std::set<std::string> unique_ons;
    for (const TimemapEntry& entry : timemap->entries)
        unique_ons.insert(entry.note_on.begin(), entry.note_on.end());
    CHECK(unique_ons.size() == 645); // + 49 rests = the importer's 694 notes

    // The join: every timemap on-id resolves to draw ops; x strictly forward.
    FlowController flow;
    REQUIRE(flow.build(*timemap, *strip, error));
    CHECK(flow.join_miss_count() == 0);
    CHECK(flow.non_monotonic_count() == 0);
    CHECK(flow.marking_qpm() == 130.0);
    CHECK(flow.max_tempo_qpm() == Catch::Approx(130.0 * FlowController::kMaxTempoFrac));

    // Playhead smoothness: grace clusters folded (no anchor pair inside the
    // fold window), and no segment sweeps x at a teleport rate — the piece's
    // ornaments used to produce ~10x-median spurts (~half a bar in ~30ms).
    {
        std::vector<double> speeds;
        for (size_t i = 1; i < flow.onsets().size(); ++i)
        {
            const double dq = flow.onsets()[i].qstamp - flow.onsets()[i - 1].qstamp;
            CHECK(dq >= FlowController::kGraceFoldQ);
            speeds.push_back((flow.onsets()[i].x - flow.onsets()[i - 1].x) / dq);
        }
        std::sort(speeds.begin(), speeds.end());
        CHECK(speeds.back() < 4.0 * speeds[speeds.size() / 2]);
    }

    ScoreHighlightState highlight;
    highlight.build(*strip);
    for (const std::string& id : unique_ons)
        CHECK(highlight.ops_for(id) != nullptr);
}

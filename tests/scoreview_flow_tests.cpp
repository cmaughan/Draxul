// Conveyor milestone C0-C2 (plans/scoreview-conveyor.md): flow layout mode,
// timemap parsing, the FlowController transport/geometry, and the highlight
// overlay — plus the live Grieg id-join acceptance tests.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

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

    ScoreHighlightState highlight;
    highlight.build(*strip);
    for (const std::string& id : unique_ons)
        CHECK(highlight.ops_for(id) != nullptr);
}

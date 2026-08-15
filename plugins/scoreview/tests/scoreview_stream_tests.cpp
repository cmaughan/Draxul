// Stream milestone S2 (plans/scoreview-stream.md): the rolling window —
// source slicing with attribute-state injection, engrave-equivalence of a
// mid-piece window against the monolithic strip, and the FlowController
// carry APIs that move the game across window rebuilds.

#include <catch2/catch_all.hpp>

#include "support/scoreview_engrave_helpers.h"

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace draxul::scoreview;

TEST_CASE("slicer indexes the Grieg and its bar geometry", "[scoreview][stream]")
{
    SourceSlicer slicer;
    std::string error;
    REQUIRE(slicer.load(read_grieg_xml(), error));
    CHECK(slicer.bar_count() == 79);
    CHECK(slicer.bar_start_q(0) == Catch::Approx(0.0));
    CHECK(slicer.bar_start_q(1) == Catch::Approx(3.0)); // 3/4 waltz
    CHECK(slicer.bar_quarters(10) == Catch::Approx(3.0));
    CHECK(slicer.bar_at(0.5) == 0);
    CHECK(slicer.bar_at(3.0) == 1);
    CHECK(slicer.bar_at(10000.0) == 78); // clamped
}

TEST_CASE("a mid-piece window engraves identically to the monolith", "[scoreview][stream]")
{
    const std::string xml = read_grieg_xml();
    SourceSlicer slicer;
    std::string error;
    REQUIRE(slicer.load(xml, error));

    const int first = 10;
    const int count = 8;
    const double offset = slicer.bar_start_q(first);
    const double window_end = slicer.bar_start_q(first + count);

    const auto full = engrave_onsets(xml);
    const auto window = engrave_onsets(slicer.window_xml(first, count));
    REQUIRE_FALSE(window.empty());

    // Every full-piece onset in [offset, end) appears in the window at the
    // shifted qstamp with the SAME pitches — engraving equivalence, the
    // property that makes windowed judgment identical to monolithic.
    int compared = 0;
    for (const auto& [q, pitches] : full)
    {
        if (q < offset - 1e-6 || q >= window_end - 1e-6)
            continue;
        const auto found = window.find(q - offset);
        if (found == window.end())
        {
            // Tolerate float formatting differences via a near lookup.
            bool near = false;
            for (const auto& [wq, wp] : window)
                near |= std::abs(wq - (q - offset)) < 1e-4 && wp == pitches;
            INFO("missing onset at " << q);
            CHECK(near);
        }
        else
        {
            CHECK(found->second == pitches);
        }
        ++compared;
    }
    CHECK(compared > 20); // 8 waltz bars carry ~28 distinct onsets
    // And nothing extra: the window holds exactly the piece's onsets.
    CHECK(window.size() == static_cast<size_t>(compared));
}

namespace
{

// Clef glyphs drawn by an engraving of `xml`, in document order (SMuFL:
// E050 = G clef, E062 = F clef, E07A/E07C = small change clefs).
std::vector<std::string> engraved_clef_glyphs(const std::string& xml)
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(xml, error));
    const std::string svg = engine->render_page_svg(1);

    std::vector<std::string> glyphs;
    size_t pos = 0;
    while ((pos = svg.find("class=\"clef\"", pos)) != std::string::npos)
    {
        const size_t href = svg.find("href=\"#", pos);
        if (href == std::string::npos)
            break;
        glyphs.push_back(svg.substr(href + 7, 4));
        pos = href;
    }
    return glyphs;
}

} // namespace

TEST_CASE("window head on an attribute-re-declaring bar keeps the bar's own clef and key",
    "[scoreview][stream]")
{
    // The Grieg's staff 2 goes treble at measure 37 and returns to bass at
    // measure 53 (which also drops the key back to no sharps). A window whose
    // HEAD is that re-declaring measure must not have the injected pre-bar
    // state (treble, 3 sharps) shadow the measure's own declarations —
    // Verovio resolves same-position duplicates first-wins, which rendered
    // the left hand on ledger lines below a treble staff.
    const std::string xml = read_grieg_xml();
    SourceSlicer slicer;
    std::string error;
    REQUIRE(slicer.load(xml, error));

    const std::string window = slicer.window_xml(52, 19); // head = measure 53
    REQUIRE_FALSE(window.empty());

    // XML level: exactly one clef per staff and one key before the head
    // measure's first note (the injected block only fills the gaps).
    const size_t head_end = window.find("<note");
    REQUIRE(head_end != std::string::npos);
    const std::string head = window.substr(0, head_end);
    const auto count_of = [&head](const std::string& needle) {
        size_t n = 0;
        for (size_t pos = head.find(needle); pos != std::string::npos;
            pos = head.find(needle, pos + needle.size()))
            ++n;
        return n;
    };
    CHECK(count_of("<clef number=\"2\"") == 1);
    CHECK(count_of("<key") == 1);
    CHECK(head.find("<fifths>0</fifths>") != std::string::npos); // the bar's own key

    // Engraved level: staff 2's head clef is the bass clef.
    const auto clefs = engraved_clef_glyphs(window);
    REQUIRE(clefs.size() >= 2);
    CHECK(clefs[0] == "E050"); // staff 1 treble
    CHECK(clefs[1] == "E062"); // staff 2 bass — the bar's own return wins
}

TEST_CASE("window head inside the treble passage still injects the changed clef",
    "[scoreview][stream]")
{
    // Heads at bars whose measures declare nothing must keep getting the full
    // injected state — here staff 2 = treble (the mid-piece change is in
    // force) with the bass-clef return engraved mid-window at measure 53.
    const std::string xml = read_grieg_xml();
    SourceSlicer slicer;
    std::string error;
    REQUIRE(slicer.load(xml, error));

    const std::string window = slicer.window_xml(40, 19); // measures 41..59
    REQUIRE_FALSE(window.empty());
    const auto clefs = engraved_clef_glyphs(window);
    REQUIRE(clefs.size() >= 3);
    CHECK(clefs[0] == "E050"); // staff 1 treble
    CHECK(clefs[1] == "E050"); // staff 2 treble (injected mid-piece state)
    // The return to bass at measure 53 engraves as a small change clef.
    CHECK(std::find(clefs.begin(), clefs.end(), "E07C") != clefs.end());
}

TEST_CASE("window 0 preserves the piece opening exactly", "[scoreview][stream]")
{
    const std::string xml = read_grieg_xml();
    SourceSlicer slicer;
    std::string error;
    REQUIRE(slicer.load(xml, error));

    const auto full = engrave_onsets(xml);
    const auto window = engrave_onsets(slicer.window_xml(0, 10));
    const double window_end = slicer.bar_start_q(10);
    for (const auto& [q, pitches] : window)
    {
        const auto found = full.find(q);
        REQUIRE(found != full.end());
        CHECK(found->second == pitches);
        CHECK(q < window_end + 1e-6);
    }
}

TEST_CASE("carry state and preset verdicts move the game across rebuilds",
    "[scoreview][stream]")
{
    // Two "windows" of the same synthetic world stand in for a rebuild.
    const auto make_flow = [](FlowController& flow) {
        ScoreDrawList strip;
        strip.canvas_size = { 2000.0f, 500.0f };
        std::string json = "[";
        for (int i = 0; i < 6; ++i)
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
        REQUIRE(flow.build(*timemap, strip, error));
        flow.prepare_gates([](const std::string&) { return 60; }, {});
        flow.set_mode(FlowController::TransportMode::Roll);
        flow.play();
    };

    FlowController first;
    make_flow(first);
    for (int i = 0; i < 100000 && first.position_q() < 1.0; ++i)
        first.advance(0.016);
    first.judge({ { 60, 0.0 } }); // hit onset 1; onset 0 missed by now
    for (int i = 0; i < 100000 && first.position_q() < 2.2; ++i)
        first.advance(0.016);
    const FlowController::CarryState carried = first.carry_state();
    CHECK(carried.score > 0);
    CHECK(carried.miss_count >= 1);

    FlowController second;
    make_flow(second);
    second.set_marking_qpm(100.0);
    second.restore_carry(carried);
    CHECK(second.score() == carried.score);
    CHECK(second.miss_count() == carried.miss_count);
    CHECK(second.accuracy_ema() == Catch::Approx(carried.accuracy_ema));

    // Replay the earned verdicts onto the fresh gates, then fast-forward.
    second.preset_verdict(0.0, 60, FlowController::NoteVerdict::Missed);
    second.preset_verdict(1.0, 60, FlowController::NoteVerdict::Correct);
    second.fast_forward_resolved(2.2);
    second.seek(2.2);
    const auto changes = second.take_verdict_update().changes;
    CHECK(changes.size() >= 2); // repaint diff for the fresh strip
    CHECK(second.gates()[0].notes[0].verdict == FlowController::NoteVerdict::Missed);
    CHECK(second.gates()[1].notes[0].verdict == FlowController::NoteVerdict::Correct);
    // Fast-forward counted nothing new.
    CHECK(second.miss_count() == carried.miss_count);
    CHECK(second.score() == carried.score);

    // The game continues seamlessly: the next onset judges normally.
    for (int i = 0; i < 100000 && second.position_q() < 3.0; ++i)
        second.advance(0.016);
    second.judge({ { 60, 0.0 } });
    CHECK(second.gates()[3].notes[0].verdict == FlowController::NoteVerdict::Correct);
    CHECK(second.score() > carried.score);
}

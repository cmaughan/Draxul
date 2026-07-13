// Stream milestone S2 (plans/scoreview-stream.md): the rolling window —
// source slicing with attribute-state injection, engrave-equivalence of a
// mid-piece window against the monolithic strip, and the FlowController
// carry APIs that move the game across window rebuilds.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/svg_score_interpreter.h>
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

// qstamp -> sorted sounding pitches, via a Verovio load of `xml`.
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

} // namespace

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

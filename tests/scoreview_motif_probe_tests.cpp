// Hidden probe: why is the opening right-hand gesture not a motif?
#include <catch2/catch_all.hpp>
#include <cstdio>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/verovio_layout_engine.h>
#include <fstream>
#include <sstream>

using namespace draxul::scoreview;

namespace
{
void probe(const char* path, double qpb)
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    std::ifstream stream(std::string(DRAXUL_PROJECT_ROOT) + path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    REQUIRE(engine->load(buffer.str(), error));
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    const auto onsets = analysis_onsets_from_timemap(
        *timemap, [&](const std::string& id) { return engine->midi_pitch_for_element(id); });
    const auto melody = skyline_melody(onsets);
    std::printf("\n===== %s (qpb %.1f): %zu onsets, %zu melody notes =====\n", path, qpb,
        onsets.size(), melody.size());
    std::printf("melody through bar 10 (q < %.1f):\n", 10.0 * qpb);
    for (size_t i = 0; i < melody.size() && melody[i].qstamp < 10.0 * qpb; ++i)
    {
        const double dur = melody[i].voice < onsets[melody[i].onset].durations.size()
            ? onsets[melody[i].onset].durations[melody[i].voice]
            : 0.0;
        const double ioi = i + 1 < melody.size() ? melody[i + 1].qstamp - melody[i].qstamp : 0.0;
        std::printf("  bar %4.1f  q %6.2f  pitch %3d  dur %5.2f  ioi %5.2f%s\n",
            melody[i].qstamp / qpb + 1.0, melody[i].qstamp, melody[i].pitch, dur, ioi,
            ioi - dur > 1.0 + 1e-9 ? "  <BREAK after>" : "");
    }
    const PieceProfile profile = analyze_piece(onsets, qpb, std::nullopt);
    std::printf("motifs (%zu):\n", profile.motifs.size());
    for (size_t m = 0; m < profile.motifs.size(); ++m)
    {
        const auto& motif = profile.motifs[m];
        std::printf("  %zu: x%d len %zu  first bar %.1f  occ bars:", m, motif.count,
            motif.intervals.size() + 1, motif.first_q / qpb + 1.0);
        for (const double q : motif.occurrences_q)
            std::printf(" %.1f", q / qpb + 1.0);
        std::printf("\n     intervals:");
        for (const int v : motif.intervals)
            std::printf(" %+d", v);
        std::printf("  iois:");
        for (const int v : motif.ioi_twelfths)
            std::printf(" %d", v);
        std::printf("\n");
    }
}
} // namespace

TEST_CASE("motif probe", "[.][probe-motif]")
{
    probe("/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl", 3.0);
    probe("/tests/fixtures/musicxml/swan_lake.musicxml", 4.0);
}

// Stream milestone S1 (plans/scoreview-stream.md): piece analysis — key
// estimation, chord inventory with successors, motif mining, and rhythm
// figures. Synthetic fixtures with known answers, then the real Grieg.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace draxul::scoreview;

namespace
{

AnalysisOnset at(double q, std::initializer_list<int> pitches)
{
    AnalysisOnset onset;
    onset.qstamp = q;
    onset.pitches = pitches;
    return onset;
}

} // namespace

TEST_CASE("analysis finds the key of a scale", "[scoreview][analysis]")
{
    // C major scale up and down, quarters.
    std::vector<AnalysisOnset> onsets;
    const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60 };
    for (size_t i = 0; i < std::size(scale); ++i)
        onsets.push_back(at(static_cast<double>(i), { scale[i] }));

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    CHECK(key_name(profile.global_key.tonic_pc, profile.global_key.minor) == "C major");
    CHECK(profile.global_key.confidence > 0.0);
    CHECK(profile.key_sections.empty()); // one key: no section report
}

TEST_CASE("analysis identifies chords and their nearings", "[scoreview][analysis]")
{
    // Am - Dm - E - Am, twice (block triads).
    std::vector<AnalysisOnset> onsets;
    const std::vector<std::vector<int>> progression = {
        { 57, 60, 64 }, { 50, 53, 57 }, { 52, 56, 59 }, { 57, 60, 64 }
    };
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        for (size_t i = 0; i < progression.size(); ++i)
            onsets.push_back(at(repeat * 4.0 + i, { progression[i][0], progression[i][1], progression[i][2] }));
    }

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    REQUIRE_FALSE(profile.chords.empty());
    CHECK(profile.chords.front().name == "A min"); // most frequent
    bool e_to_a = false;
    for (const PieceProfile::Chord& chord : profile.chords)
    {
        if (chord.name != "E maj")
            continue;
        for (const auto& [next, count] : chord.next)
            e_to_a |= next == "A min";
    }
    CHECK(e_to_a); // the cadence is in the join table
    // Minor key despite the E major chords (harmonic minor's dominant).
    CHECK(key_name(profile.global_key.tonic_pc, profile.global_key.minor) == "A minor");
}

TEST_CASE("analysis borrows the bar bass for waltz dyads", "[scoreview][analysis]")
{
    // Oom-pah-pah: bass A2 on beat 1, then two {E3,C4} dyads — A minor.
    std::vector<AnalysisOnset> onsets;
    for (int bar = 0; bar < 4; ++bar)
    {
        onsets.push_back(at(bar * 3.0, { 45 }));
        onsets.push_back(at(bar * 3.0 + 1.0, { 52, 60 }));
        onsets.push_back(at(bar * 3.0 + 2.0, { 52, 60 }));
    }
    const PieceProfile profile = analyze_piece(onsets, 3.0, 0);
    REQUIRE_FALSE(profile.chords.empty());
    CHECK(profile.chords.front().name == "A min");
}

TEST_CASE("analysis recognizes rhythm figures including triplets", "[scoreview][analysis]")
{
    std::vector<AnalysisOnset> onsets;
    // Beats 0-3: triplets; beats 4-7: straight eighths; beat 8: quarter.
    for (int beat = 0; beat < 4; ++beat)
    {
        onsets.push_back(at(beat, { 60 }));
        onsets.push_back(at(beat + 1.0 / 3.0, { 62 }));
        onsets.push_back(at(beat + 2.0 / 3.0, { 64 }));
    }
    for (int beat = 4; beat < 8; ++beat)
    {
        onsets.push_back(at(beat, { 60 }));
        onsets.push_back(at(beat + 0.5, { 62 }));
    }
    onsets.push_back(at(8.0, { 60 }));

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    REQUIRE_FALSE(profile.figures.empty());
    int triplet_count = 0;
    int eighths_count = 0;
    for (const PieceProfile::Figure& figure : profile.figures)
    {
        if (figure.name == "triplet")
            triplet_count = figure.count;
        if (figure.name == "eighths")
            eighths_count = figure.count;
    }
    CHECK(triplet_count == 4);
    CHECK(eighths_count == 4);
}

TEST_CASE("analysis mines recurring melodic motifs", "[scoreview][analysis]")
{
    // The motif +2 +2 +1 -5 appears three times amid noise.
    std::vector<AnalysisOnset> onsets;
    double q = 0.0;
    const auto add_run = [&](std::initializer_list<int> pitches) {
        for (const int pitch : pitches)
            onsets.push_back(at(q++, { pitch }));
    };
    add_run({ 60, 62, 64, 65, 60 });
    add_run({ 71, 67 }); // noise
    add_run({ 62, 64, 66, 67, 62 });
    add_run({ 59 }); // noise
    add_run({ 65, 67, 69, 70, 65 });

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    bool found = false;
    for (const PieceProfile::Motif& motif : profile.motifs)
    {
        if (motif.intervals == std::vector<int>{ 2, 2, 1, -5 })
            found = motif.count >= 3;
    }
    CHECK(found);
}

TEST_CASE("analysis profiles the real Grieg", "[scoreview][analysis]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(
        std::string(DRAXUL_VEROVIO_DATA_DIR), error);
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

    std::vector<AnalysisOnset> onsets;
    for (const TimemapEntry& entry : timemap->entries)
    {
        AnalysisOnset onset;
        onset.qstamp = entry.qstamp;
        for (const std::string& id : entry.note_on)
        {
            const int pitch = engine->midi_pitch_for_element(id);
            if (pitch >= 0)
                onset.pitches.push_back(pitch);
        }
        if (!onset.pitches.empty())
            onsets.push_back(std::move(onset));
    }
    REQUIRE(onsets.size() > 100);

    const PieceProfile profile = analyze_piece(onsets, 3.0, 0);
    // Ground truth: the waltz is in A minor (with an A major episode).
    CHECK(key_name(profile.global_key.tonic_pc, profile.global_key.minor) == "A minor");
    REQUIRE_FALSE(profile.chords.empty());
    bool has_a_minor_chord = false;
    for (const PieceProfile::Chord& chord : profile.chords)
        has_a_minor_chord |= chord.name == "A min";
    CHECK(has_a_minor_chord);
    CHECK_FALSE(profile.figures.empty());
    CHECK_FALSE(profile.motifs.empty());
    CHECK_FALSE(profile.serialize().empty());
}

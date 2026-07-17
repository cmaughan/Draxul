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

// Structure fixtures must isolate ONE boundary cue, which constrains the
// filler melody more than it first appears:
//  - single notes only, so no chord forms and no cadence can be read;
//  - keyed to the ABSOLUTE beat with a period that divides the 8-bar key
//    window and its 4-bar hop, so every window sees identical pitch-class
//    content and the key estimate cannot waver. A wavering estimate reports
//    a modulation, and a modulation is itself a boundary cue — random
//    pitches (chromatic OR diatonic) fail this and quietly invent phrases.
// A repeating cycle does mint a motif, but a motif restart is deliberately
// too weak to open a phrase on its own, so it cannot pollute the fixtures.
int triad_cycle(int beat_index)
{
    static constexpr int kCycle[4] = { 60, 64, 67, 64 }; // C E G E
    return kCycle[((beat_index % 4) + 4) % 4];
}

// Four 4-bar phrases in 4/4, each breathing for its last two beats. The gap
// across the barline is the only cue present.
std::vector<AnalysisOnset> four_phrases_with_rests()
{
    std::vector<AnalysisOnset> onsets;
    for (int bar = 0; bar < 16; ++bar)
    {
        for (int beat = 0; beat < 4; ++beat)
        {
            if (bar % 4 == 3 && beat >= 2)
                continue; // the phrase breathes
            onsets.push_back(at(bar * 4.0 + beat, { triad_cycle(bar * 4 + beat) }));
        }
    }
    return onsets;
}

// The real waltz, engraved and analyzed once (the Verovio round-trip is slow).
const PieceProfile& grieg_profile()
{
    static PieceProfile profile = [] {
        std::string error;
        auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
        INFO(error);
        REQUIRE(engine != nullptr);
        LayoutOptions options;
        options.mode = LayoutMode::Flow;
        engine->set_options(options);
        std::ifstream stream(std::string(DRAXUL_PROJECT_ROOT)
                + "/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl",
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
        return analyze_piece(onsets, 3.0, 0);
    }();
    return profile;
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

TEST_CASE("analysis records every motif occurrence", "[scoreview][analysis][structure]")
{
    // The motif +2 +2 +1 -5 appears three times; occurrences must all be
    // recorded, ascending — the overlay marks each one.
    std::vector<AnalysisOnset> onsets;
    double q = 0.0;
    const auto add_run = [&](std::initializer_list<int> pitches) {
        for (const int pitch : pitches)
            onsets.push_back(at(q++, { pitch }));
    };
    add_run({ 60, 62, 64, 65, 60 });
    add_run({ 71, 67 });
    add_run({ 62, 64, 66, 67, 62 });
    add_run({ 59 });
    add_run({ 65, 67, 69, 70, 65 });

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    const PieceProfile::Motif* found = nullptr;
    for (const PieceProfile::Motif& motif : profile.motifs)
    {
        if (motif.intervals == std::vector<int>{ 2, 2, 1, -5 })
            found = &motif;
    }
    REQUIRE(found != nullptr);
    CHECK(found->occurrences_q.size() == static_cast<size_t>(found->count));
    CHECK(found->occurrences_q.front() == Catch::Approx(found->first_q));
    CHECK(std::is_sorted(found->occurrences_q.begin(), found->occurrences_q.end()));
}

TEST_CASE("identical phrases are marked as restatements", "[scoreview][analysis][structure]")
{
    // The rest-separated fixture repeats ONE melody four times: phrases 1-3
    // must chain back to phrase 0, untransposed — repeated material is not
    // new learning, and the composer needs to know.
    const PieceProfile profile = analyze_piece(four_phrases_with_rests(), 4.0, 0);
    REQUIRE(profile.phrases.size() == 4);
    CHECK(profile.phrases[0].repeat_of == -1);
    for (size_t i = 1; i < profile.phrases.size(); ++i)
    {
        CHECK(profile.phrases[i].repeat_of == 0);
        CHECK_FALSE(profile.phrases[i].transposed);
    }
}

TEST_CASE("a transposed restatement is flagged as such", "[scoreview][analysis][structure]")
{
    // Phrase A, then A up a fourth: same intervals and rhythm, different
    // opening pitch. Rest gaps carry the boundary.
    std::vector<AnalysisOnset> onsets;
    const auto add_phrase = [&](double start_q, int base) {
        const int shape[] = { 0, 4, 7, 4, 0, 7, 4, 0, 2, 4, 5, 4, 0, 2 };
        for (size_t i = 0; i < std::size(shape); ++i)
            onsets.push_back(at(start_q + static_cast<double>(i), { base + shape[i] }));
    };
    add_phrase(0.0, 60); // bars 0-3 (14 quarters, then rests)
    add_phrase(16.0, 65); // bars 4-7, up a fourth

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    REQUIRE(profile.phrases.size() == 2);
    CHECK(profile.phrases[1].repeat_of == 0);
    CHECK(profile.phrases[1].transposed);
}

TEST_CASE("a sustained melody shadows the accompaniment beneath it",
    "[scoreview][analysis][structure]")
{
    // Waltz texture: a long melody note rings across the bar while low
    // accompaniment chords strike under it. Without durations the "top pitch
    // per onset" melody interleaves the accompaniment — motifs then carry
    // huge melody<->accompaniment leaps (the bug the coloring exposed).
    // With durations, the skyline shadows those strikes and the motif is the
    // pure melodic line.
    std::vector<AnalysisOnset> onsets;
    const auto melody_note = [&](double q, int pitch) {
        AnalysisOnset onset;
        onset.qstamp = q;
        onset.pitches = { pitch };
        onset.durations = { 2.0 }; // rings across the accompaniment
        onsets.push_back(onset);
    };
    const auto accompaniment = [&](double q) {
        AnalysisOnset onset;
        onset.qstamp = q;
        onset.pitches = { 40, 47 }; // far below the melody
        onset.durations = { 1.0, 1.0 };
        onsets.push_back(onset);
    };
    // Three statements of a 5-note gesture (intervals +2 +2 +1 -5), each
    // note sustained over one accompaniment strike.
    const int shape[] = { 72, 74, 76, 77, 72 };
    for (int statement = 0; statement < 3; ++statement)
    {
        const double base = statement * 10.0;
        for (size_t i = 0; i < std::size(shape); ++i)
        {
            melody_note(base + 2.0 * static_cast<double>(i), shape[i]);
            accompaniment(base + 2.0 * static_cast<double>(i) + 1.0);
        }
    }

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    bool found_clean = false;
    for (const PieceProfile::Motif& motif : profile.motifs)
    {
        if (motif.intervals == std::vector<int>{ 2, 2, 1, -5 })
            found_clean = motif.count >= 3;
        for (const int interval : motif.intervals)
        {
            INFO("polluted motif interval " << interval);
            CHECK(std::abs(interval) < 12); // no melody<->accompaniment leaps
        }
    }
    CHECK(found_clean);
}

TEST_CASE("timemap onsets carry durations for the skyline", "[scoreview][analysis][structure]")
{
    // The shared conversion: note_off minus note_on becomes the duration the
    // skyline shadows with; ids stay parallel for the overlay's coloring.
    Timemap timemap;
    TimemapEntry on;
    on.qstamp = 0.0;
    on.note_on = { "mel", "bass" };
    TimemapEntry off_bass;
    off_bass.qstamp = 1.0;
    off_bass.note_off = { "bass" };
    off_bass.note_on = { "acc" };
    TimemapEntry off_rest;
    off_rest.qstamp = 3.0;
    off_rest.note_off = { "mel", "acc" };
    timemap.entries = { on, off_bass, off_rest };

    const auto pitch_of = [](const std::string& id) {
        if (id == "mel")
            return 72;
        if (id == "bass")
            return 40;
        if (id == "acc")
            return 52;
        return -1;
    };
    std::vector<std::vector<std::string>> ids;
    const std::vector<AnalysisOnset> onsets
        = analysis_onsets_from_timemap(timemap, pitch_of, &ids);
    REQUIRE(onsets.size() == 2);
    CHECK(onsets[0].pitches == std::vector<int>{ 72, 40 });
    CHECK(onsets[0].durations[0] == Catch::Approx(3.0)); // mel rings to q3
    CHECK(onsets[0].durations[1] == Catch::Approx(1.0));
    CHECK(ids[0] == std::vector<std::string>{ "mel", "bass" });

    // And the skyline: the q1 accompaniment strike (52) is shadowed by the
    // melody (72) still ringing.
    const std::vector<MelodyNote> melody = skyline_melody(onsets);
    REQUIRE(melody.size() == 1);
    CHECK(melody[0].pitch == 72);
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

// --- Structure: phrases + sections (C0, plans/scoreview-composer.md) ------
// The composer must chunk on musical structure, never on fixed windows, so
// these fixtures isolate one boundary cue at a time and then check the
// honesty property: unphrased material must NOT report phrasing.

TEST_CASE("analysis finds phrase boundaries at rests", "[scoreview][analysis][structure]")
{
    const PieceProfile profile = analyze_piece(four_phrases_with_rests(), 4.0, 0);

    REQUIRE(profile.phrases.size() == 4);
    CHECK(profile.phrases[0].start_bar == 0);
    CHECK(profile.phrases[1].start_bar == 4);
    CHECK(profile.phrases[2].start_bar == 8);
    CHECK(profile.phrases[3].start_bar == 12);
    CHECK(profile.phrases[3].end_bar == 16);
    // The gap, not the 4-bar prior, carried these.
    CHECK(profile.structure_confidence > 0.5);
}

TEST_CASE("analysis finds a phrase boundary at an authentic cadence",
    "[scoreview][analysis][structure]")
{
    // I - IV - V - I twice, one chord per bar: no rests, no motifs, so the
    // cadence closing bar 4 into bar 5 is the only cue.
    const std::vector<std::vector<int>> progression = {
        { 60, 64, 67 }, // C
        { 65, 69, 72 }, // F
        { 67, 71, 74 }, // G
        { 60, 64, 67 }, // C
    };
    std::vector<AnalysisOnset> onsets;
    for (int bar = 0; bar < 8; ++bar)
    {
        AnalysisOnset onset;
        onset.qstamp = bar * 4.0;
        onset.pitches = progression[static_cast<size_t>(bar % 4)];
        onsets.push_back(onset);
    }

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    REQUIRE(profile.phrases.size() == 2);
    CHECK(profile.phrases[0].start_bar == 0);
    CHECK(profile.phrases[1].start_bar == 4);
    // The dominant resolving across the barline is full evidence; the half
    // cadence closing bar 3 must NOT have split the phrase.
    CHECK(profile.phrases[1].confidence > 0.9);
}

TEST_CASE("analysis does not invent phrases it cannot see", "[scoreview][analysis][structure]")
{
    // A uniform stream: no rests, no cadences, no motifs. The hypermetric
    // prior must never masquerade as detected structure.
    std::vector<AnalysisOnset> onsets;
    for (int i = 0; i < 48; ++i) // 12 bars of 4/4, under the max phrase length
        onsets.push_back(at(i * 1.0, { triad_cycle(i) }));

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    // Fixture preconditions: if any of these trip, the fixture grew a cue and
    // the test below would be measuring the wrong thing.
    REQUIRE(profile.key_sections.empty()); // no modulation cue
    REQUIRE(profile.chords.empty()); // no cadence cue

    CHECK(profile.phrases.size() == 1);
    CHECK(profile.structure_confidence == Catch::Approx(0.0));
}

TEST_CASE("unphrased material still chunks, but reports it guessed",
    "[scoreview][analysis][structure]")
{
    // Longer than the maximum phrase length with no cues at all: the split
    // must yield workable chunks WITHOUT claiming structure, so the composer
    // keeps its fixed-slice fallback (StreamComposer::kMinStructureConfidence).
    std::vector<AnalysisOnset> onsets;
    for (int i = 0; i < 96; ++i) // 24 bars of 4/4
        onsets.push_back(at(i * 1.0, { triad_cycle(i) }));

    const PieceProfile profile = analyze_piece(onsets, 4.0, 0);
    CHECK(profile.phrases.size() > 1); // chunked
    CHECK(profile.structure_confidence < 0.35); // but honestly
}

TEST_CASE("bar positions carry serial position within the phrase",
    "[scoreview][analysis][structure]")
{
    const PieceProfile profile = analyze_piece(four_phrases_with_rests(), 4.0, 0);
    REQUIRE(profile.bar_positions.size() == 16);

    // Phrase openings are the anchors; the tail is where recall collapses.
    CHECK(profile.bar_positions[0].phrase_open);
    CHECK(profile.bar_positions[0].serial_in_phrase == 0);
    CHECK(profile.bar_positions[3].serial_in_phrase == 3);
    CHECK(profile.bar_positions[3].phrase_length == 4);
    CHECK_FALSE(profile.bar_positions[3].phrase_open);
    CHECK(profile.bar_tail_fraction(0) == Catch::Approx(0.0));
    CHECK(profile.bar_tail_fraction(3) == Catch::Approx(1.0));

    REQUIRE(profile.phrase_at_bar(5) != nullptr);
    CHECK(profile.phrase_at_bar(5)->start_bar == 4);
    CHECK(profile.phrase_at_bar(-1) == nullptr);
    CHECK(profile.phrase_at_bar(999) == nullptr);
}

TEST_CASE("an unmodulating piece is honestly one section", "[scoreview][analysis][structure]")
{
    // Sections only split where the key actually moves; inventing section
    // structure would be worse than admitting we cannot see it.
    const PieceProfile profile = analyze_piece(four_phrases_with_rests(), 4.0, 0);
    REQUIRE(profile.sections.size() == 1);
    CHECK(profile.sections[0].start_bar == 0);
    CHECK(profile.sections[0].end_bar == 16);
    CHECK(profile.sections[0].phrase_ids.size() == profile.phrases.size());
    CHECK(profile.bar_positions[0].section_open);
}

TEST_CASE("the real Grieg yields usable phrase structure", "[scoreview][analysis][structure]")
{
    const PieceProfile profile = grieg_profile();
    REQUIRE_FALSE(profile.phrases.empty());
    // Phrases partition the piece with no gaps or overlaps.
    for (size_t i = 0; i + 1 < profile.phrases.size(); ++i)
        CHECK(profile.phrases[i].end_bar == profile.phrases[i + 1].start_bar);
    CHECK(profile.phrases.front().start_bar == 0);
    for (const PieceProfile::Phrase& phrase : profile.phrases)
        CHECK(phrase.end_bar > phrase.start_bar);
    // Real music must clear the composer's structure gate, or C1 would spend
    // its life in the fixed-slice fallback.
    CHECK(profile.structure_confidence >= 0.35);
    CHECK(profile.serialize().find("phrases") != std::string::npos);
}

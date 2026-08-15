// The analysis overlay (green "what the analysis discovered" annotations for
// the paged reading view): the PURE builder — note geometry + timemap +
// profile in, canvas-space markers out. Synthetic pages with known geometry
// first, then the real Grieg engraving.

#include <catch2/catch_all.hpp>

#include "support/scoreview_engrave_helpers.h"

#include <draxul/scoreview/analysis_overlay.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <string>

using namespace draxul::scoreview;

namespace
{

// A fabricated "engraving": `bars_per_page` bars per page, two systems per
// page (bars alternate rows every `bars_per_row`), one note glyph per beat.
struct SyntheticScore
{
    std::vector<ScoreDrawList> pages;
    Timemap timemap;
    std::vector<double> bar_starts;

    SyntheticScore(int total_bars, int bars_per_page, int bars_per_row, double qpb = 4.0,
        float row_h = 300.0f)
    {
        const float bar_w = 100.0f;
        const float staff_h = 40.0f;
        for (int bar = 0; bar < total_bars; ++bar)
        {
            bar_starts.push_back(bar * qpb);
            const int page = bar / bars_per_page;
            if (page >= static_cast<int>(pages.size()))
            {
                pages.emplace_back();
                pages.back().canvas_size = { bars_per_page * bar_w / 2.0f, 2.0f * row_h };
            }
            const int in_page = bar % bars_per_page;
            const int row = in_page / bars_per_row;
            for (int beat = 0; beat < static_cast<int>(qpb); ++beat)
            {
                const std::string id
                    = "note-" + std::to_string(bar) + "-" + std::to_string(beat);
                GlyphInstance glyph;
                glyph.xform.e = (in_page % bars_per_row) * bar_w + beat * (bar_w / qpb);
                glyph.xform.f = row * row_h + 100.0f + (beat % 2 == 0 ? 0.0f : staff_h);
                glyph.element_id = id;
                pages[static_cast<size_t>(page)].glyphs.push_back(glyph);

                TimemapEntry entry;
                entry.qstamp = bar * qpb + beat;
                entry.note_on.push_back(id);
                timemap.entries.push_back(entry);
            }
        }
        timemap.duration_q = total_bars * qpb;
    }
};

PieceProfile two_phrase_profile(int total_bars, int split_bar)
{
    PieceProfile profile;
    profile.global_key.tonic_pc = 0;
    profile.global_key.confidence = 0.4;
    profile.structure_confidence = 0.8;
    PieceProfile::Phrase a;
    a.start_bar = 0;
    a.end_bar = split_bar;
    PieceProfile::Phrase b;
    b.start_bar = split_bar;
    b.end_bar = total_bars;
    b.confidence = 0.8;
    profile.phrases = { a, b };
    PieceProfile::Section section;
    section.start_bar = 0;
    section.end_bar = total_bars;
    section.phrase_ids = { 0, 1 };
    profile.sections = { section };
    return profile;
}

} // namespace

TEST_CASE("overlay maps bars onto their engraved geometry", "[scoreview][overlay]")
{
    // 8 bars, one page, 4 bars per row: two rows of four.
    SyntheticScore score(8, 8, 4);
    const PieceProfile profile = two_phrase_profile(8, 4);
    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    REQUIRE(overlay.pages.size() == 1);
    const auto& bars = overlay.pages[0].bars;
    REQUIRE(bars.size() == 8);
    // Ascending bars, boxes where the glyphs were placed.
    CHECK(bars[0].bar == 0);
    CHECK(bars[0].min.x == Catch::Approx(0.0f));
    CHECK(bars[4].bar == 4);
    CHECK(bars[4].min.x == Catch::Approx(0.0f)); // row 2 restarts at x 0
    CHECK(bars[4].min.y > bars[0].max.y); // and sits below row 1
    CHECK(overlay.staff_height == Catch::Approx(40.0f));
    CHECK_FALSE(overlay.banner.empty());
}

TEST_CASE("phrase spans follow system rows and mark their opening", "[scoreview][overlay]")
{
    SyntheticScore score(8, 8, 4);
    const PieceProfile profile = two_phrase_profile(8, 4);
    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    const auto& spans = overlay.pages[0].phrases;
    // Phrase 0 covers row 1 exactly; phrase 1 covers row 2: one span each.
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].phrase_id == 0);
    CHECK(spans[0].opens_phrase);
    CHECK(spans[0].opens_section); // section starts at bar 0
    CHECK(spans[0].confidence == Catch::Approx(0.0)); // opening phrase: no evidence
    CHECK(spans[1].phrase_id == 1);
    CHECK(spans[1].opens_phrase);
    CHECK_FALSE(spans[1].opens_section);
    CHECK(spans[1].confidence == Catch::Approx(0.8));
    // The span line floats above its row's notes.
    CHECK(spans[0].y < overlay.pages[0].bars[0].min.y);
    CHECK(spans[1].y < overlay.pages[0].bars[4].min.y);
    CHECK(spans[1].y > spans[0].y); // rows stack
    // Extent: from the row's first bar to its last.
    CHECK(spans[0].x0 == Catch::Approx(overlay.pages[0].bars[0].min.x));
    CHECK(spans[0].x1 == Catch::Approx(overlay.pages[0].bars[3].max.x));
}

TEST_CASE("a phrase wrapping rows yields one span per row, labeled once",
    "[scoreview][overlay]")
{
    // One 8-bar phrase over two rows: two spans, only the first opens.
    SyntheticScore score(8, 8, 4);
    PieceProfile profile = two_phrase_profile(8, 8);
    profile.phrases.pop_back();
    profile.phrases[0].end_bar = 8;
    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    const auto& spans = overlay.pages[0].phrases;
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].opens_phrase);
    CHECK_FALSE(spans[1].opens_phrase);
    CHECK(spans[0].phrase_id == spans[1].phrase_id);
}

TEST_CASE("phrases spanning pages keep page-local geometry", "[scoreview][overlay]")
{
    // 8 bars over two pages (4 per page, one row each); one phrase across both.
    SyntheticScore score(8, 4, 4);
    PieceProfile profile = two_phrase_profile(8, 8);
    profile.phrases.pop_back();
    profile.phrases[0].end_bar = 8;
    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    REQUIRE(overlay.pages.size() == 2);
    REQUIRE(overlay.pages[0].phrases.size() == 1);
    REQUIRE(overlay.pages[1].phrases.size() == 1);
    CHECK(overlay.pages[0].phrases[0].opens_phrase);
    CHECK_FALSE(overlay.pages[1].phrases[0].opens_phrase);
}

TEST_CASE("key and motif labels land above and below their bars", "[scoreview][overlay]")
{
    SyntheticScore score(8, 8, 4);
    PieceProfile profile = two_phrase_profile(8, 4);
    // A key move at bar 4 (q 16) and one strong motif at the start.
    PieceProfile::KeySection first;
    first.start_q = 0.0;
    first.end_q = 16.0;
    first.key.tonic_pc = 0;
    PieceProfile::KeySection second;
    second.start_q = 16.0;
    second.end_q = 32.0;
    second.key.tonic_pc = 7;
    profile.key_sections = { first, second };
    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    const auto& labels = overlay.pages[0].labels;
    REQUIRE(labels.size() == 2); // the two key regions; motifs color, not label
    for (const auto& label : labels)
    {
        CHECK(label.emphasis);
        CHECK(label.text.find("(") != std::string::npos); // confidence shown
    }
    // Key labels sit above the notes.
    CHECK(labels[0].pos.y < overlay.pages[0].bars[0].min.y);
}

TEST_CASE("motif occurrences color their melody notes, same color per motif",
    "[scoreview][overlay]")
{
    SyntheticScore score(8, 8, 4);
    PieceProfile profile = two_phrase_profile(8, 4);
    PieceProfile::Motif strong;
    strong.intervals = { 2, 2 }; // 2 steps -> colors 3 consecutive notes
    strong.count = 2;
    strong.first_q = 0.0;
    strong.occurrences_q = { 0.0, 20.0 }; // bar 0 beat 0, bar 5 beat 0
    PieceProfile::Motif weak;
    weak.intervals = { -1, -1 };
    weak.count = 1;
    weak.first_q = 1.0; // overlaps the strong motif's first occurrence
    weak.occurrences_q = { 1.0 };
    profile.motifs = { strong, weak };

    // Every synthetic note sounds; the fixture has one note per entry, so
    // the melody is the entry sequence itself.
    const auto pitch_of = [](const std::string&) { return 60; };
    const AnalysisOverlay overlay = build_analysis_overlay(
        score.pages, score.timemap, profile, score.bar_starts, pitch_of);

    REQUIRE(overlay.highlights.size() == 1);
    const ScoreHighlightState& colors = overlay.highlights[0];
    // Rank 0 wears the first natural color (palette 1), rank 1 the second
    // (palette 4) — same color for the same motif at both occurrences.
    const auto guide_of = [&](const std::string& id) -> int {
        const auto* ops = colors.ops_for(id);
        if (ops == nullptr || ops->empty())
            return -1;
        return colors.glyph_guide[static_cast<size_t>(ops->front().second)];
    };
    CHECK(guide_of("note-0-0") == 2); // palette 1 -> stored as index + 1
    CHECK(guide_of("note-0-1") == 2);
    CHECK(guide_of("note-0-2") == 2);
    CHECK(guide_of("note-5-0") == 2); // second occurrence, same color
    CHECK(guide_of("note-5-1") == 2);
    // The weak motif starts at q=1 (note-0-1..0-3): 0-1/0-2 already claimed
    // by the stronger motif; only the unclaimed tail note takes its color.
    CHECK(guide_of("note-0-3") == 5); // palette 4 -> index + 1
    CHECK((guide_of("note-1-0") == -1 || guide_of("note-1-0") == 0)); // uncolored
    // The legend carries one swatch per motif with its shape.
    REQUIRE(overlay.motif_legend.size() == 2);
    CHECK(overlay.motif_legend[0].palette == 1);
    CHECK(overlay.motif_legend[0].shape == "[+2 +2]");
    CHECK(overlay.motif_legend[1].palette == 4);
}

TEST_CASE("a repeated phrase's span carries its restatement", "[scoreview][overlay]")
{
    SyntheticScore score(8, 8, 4);
    PieceProfile profile = two_phrase_profile(8, 4);
    profile.phrases[1].repeat_of = 0;
    profile.phrases[1].transposed = true;

    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    REQUIRE(overlay.pages[0].phrases.size() == 2);
    CHECK(overlay.pages[0].phrases[0].repeat_of == -1);
    CHECK(overlay.pages[0].phrases[1].repeat_of == 0);
    CHECK(overlay.pages[0].phrases[1].transposed);
    // The wash band ('s' unique-chunks view) covers the repeated phrase's
    // row: the row extent must bracket its bars' geometry.
    const auto& repeat_span = overlay.pages[0].phrases[1];
    CHECK(repeat_span.row_y0 == Catch::Approx(overlay.pages[0].bars[4].min.y));
    CHECK(repeat_span.row_y1 == Catch::Approx(overlay.pages[0].bars[7].max.y));
    CHECK(repeat_span.row_y0 < repeat_span.row_y1);
}

TEST_CASE("tightly spaced rows never merge into one segment", "[scoreview][overlay]")
{
    // Rows 60 canvas units apart: the boxes' y-ranges sit well inside the
    // old same-row tolerance (a full staff height), which merged the systems
    // and let the unique-chunks wash spill across BOTH rows — over whatever
    // new material lived there. Row identity comes from the x-reset rule
    // now, so tight engraving must still yield one segment per row.
    SyntheticScore score(8, 8, 4, 4.0, /*row_h=*/60.0f);
    PieceProfile profile = two_phrase_profile(8, 8);
    profile.phrases.pop_back();
    profile.phrases[0].end_bar = 8;

    const AnalysisOverlay overlay
        = build_analysis_overlay(score.pages, score.timemap, profile, score.bar_starts);

    REQUIRE(overlay.pages[0].bars.size() == 8);
    CHECK(overlay.pages[0].bars[3].row == 0);
    CHECK(overlay.pages[0].bars[4].row == 1); // x reset -> next system
    const auto& spans = overlay.pages[0].phrases;
    REQUIRE(spans.size() == 2); // one per row, never one merged rectangle
    CHECK(spans[0].row_y1 < spans[1].row_y0 + 60.0f); // extents stay row-local
}

TEST_CASE("the wash never covers new material on the real Grieg",
    "[scoreview][overlay]")
{
    // The live bug: a repeated phrase's wash rectangle obscured part of a
    // NEW phrase. Invariant: no repeat segment's rect (x0..x1 over its row
    // band) may intersect any bar box belonging to unrepeated material.
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.page_size_px = { 800, 1131 };
    engine->set_options(options);
    REQUIRE(engine->load(
        read_fixture("/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl"), error));
    std::vector<ScoreDrawList> pages;
    for (int page = 1; page <= engine->page_count(); ++page)
    {
        auto list = interpret_score_svg(engine->render_page_svg(page), error);
        REQUIRE(list.has_value());
        pages.push_back(std::move(*list));
    }
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
    const PieceProfile profile = analyze_piece(onsets, 3.0, 0);
    std::vector<double> bar_starts;
    for (double q = 0.0; q < timemap->duration_q; q += 3.0)
        bar_starts.push_back(q);
    const AnalysisOverlay overlay
        = build_analysis_overlay(pages, *timemap, profile, bar_starts);

    // Which bars belong to NEW (unrepeated) phrases.
    const auto bar_is_new = [&](int bar) {
        const PieceProfile::Phrase* phrase = profile.phrase_at_bar(bar);
        return phrase == nullptr || phrase->repeat_of < 0;
    };
    bool any_repeat_segment = false;
    for (const AnalysisOverlay::Page& page : overlay.pages)
    {
        for (const AnalysisOverlay::PhraseSpan& span : page.phrases)
        {
            if (span.repeat_of < 0)
                continue;
            any_repeat_segment = true;
            for (const AnalysisOverlay::BarBox& box : page.bars)
            {
                if (!bar_is_new(box.bar))
                    continue;
                const bool x_overlap = box.min.x < span.x1 && box.max.x > span.x0;
                const bool y_overlap = box.min.y < span.row_y1 && box.max.y > span.row_y0;
                INFO("repeat P" << span.phrase_id << " segment [" << span.x0 << ".."
                                << span.x1 << "] x new bar " << box.bar + 1);
                CHECK_FALSE((x_overlap && y_overlap));
            }
        }
    }
    CHECK(any_repeat_segment); // the invariant must have actually been exercised
}

TEST_CASE("empty inputs yield an empty overlay", "[scoreview][overlay]")
{
    const PieceProfile profile;
    const Timemap timemap;
    CHECK(build_analysis_overlay({}, timemap, profile, {}).pages.empty());

    SyntheticScore score(4, 4, 4);
    const AnalysisOverlay no_bars
        = build_analysis_overlay(score.pages, score.timemap, profile, {});
    CHECK(no_bars.pages.size() == 1);
    CHECK(no_bars.pages[0].bars.empty());
}

TEST_CASE("the real Grieg builds a usable overlay", "[scoreview][overlay]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    INFO(error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.page_size_px = { 800, 1131 };
    engine->set_options(options);
    REQUIRE(engine->load(
        read_fixture("/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl"), error));

    std::vector<ScoreDrawList> pages;
    for (int page = 1; page <= engine->page_count(); ++page)
    {
        auto list = interpret_score_svg(engine->render_page_svg(page), error);
        REQUIRE(list.has_value());
        pages.push_back(std::move(*list));
    }
    REQUIRE_FALSE(pages.empty());

    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());

    // The analysis on the same axis (3/4 waltz).
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
    const PieceProfile profile = analyze_piece(onsets, 3.0, 0);
    REQUIRE_FALSE(profile.phrases.empty());

    std::vector<double> bar_starts;
    for (double q = 0.0; q < timemap->duration_q; q += 3.0)
        bar_starts.push_back(q);

    const AnalysisOverlay overlay = build_analysis_overlay(pages, *timemap, profile,
        bar_starts, [&](const std::string& id) { return engine->midi_pitch_for_element(id); });

    REQUIRE(overlay.pages.size() == pages.size());
    // Motif coloring rides the guidance channel: with motifs present, some
    // notes must carry a palette index, and the legend describes each motif.
    REQUIRE_FALSE(profile.motifs.empty());
    REQUIRE(overlay.highlights.size() == pages.size());
    size_t guided = 0;
    for (const ScoreHighlightState& page_colors : overlay.highlights)
    {
        for (const uint8_t guide : page_colors.glyph_guide)
            guided += guide != 0 ? 1 : 0;
    }
    CHECK(guided > 0);
    CHECK(overlay.motif_legend.size() == profile.motifs.size());
    size_t total_bars = 0;
    size_t total_spans = 0;
    size_t opening_spans = 0;
    for (size_t p = 0; p < overlay.pages.size(); ++p)
    {
        total_bars += overlay.pages[p].bars.size();
        total_spans += overlay.pages[p].phrases.size();
        for (const AnalysisOverlay::PhraseSpan& span : overlay.pages[p].phrases)
        {
            if (span.opens_phrase)
                ++opening_spans;
            // Geometry stays on the page's canvas. A row segment holding a
            // single note collapses to a point (x1 == x0) — the tick still
            // marks it, so degenerate spans are data, not an error.
            CHECK(span.x0 >= 0.0f);
            CHECK(span.x1 <= pages[p].canvas_size.x);
            CHECK(span.x1 >= span.x0);
        }
    }
    // Every engraved bar with notes has geometry, and every phrase opens
    // exactly once somewhere.
    CHECK(total_bars >= bar_starts.size() - 4); // a few all-rest bars allowed
    CHECK(opening_spans == profile.phrases.size());
    CHECK(total_spans >= opening_spans);
    CHECK(overlay.staff_height > 0.0f);
    CHECK(overlay.banner.find("phrases") != std::string::npos);
}

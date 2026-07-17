#pragma once

// The analysis overlay (user-requested design/debug surface): green
// annotations drawn over the paged reading view ('f') showing what the
// upfront piece analysis discovered BEFORE the composer acts on it — key
// regions, detected phrase spans with their boundary confidence, section
// starts, and the strongest motifs. Green is the contract: everything green
// is Draxul's inference about the piece, never the engraved source.
//
// Split like the rest of the module: build_analysis_overlay is PURE (draw
// lists + timemap + profile in, canvas-space geometry out) so tests can
// assert placement without a GPU; draw_analysis_overlay replays a built page
// through NanoVG.

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/score_timemap.h>

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

struct NVGcontext;

namespace draxul
{
namespace scoreview
{

struct ScoreTextFonts;

struct AnalysisOverlay
{
    // Canvas-space bounding box of one source bar's engraved notes on one
    // page (note geometry only — rests/barlines carry other element ids, so
    // an all-rest bar has no box and annotations snap to the next bar that
    // has one).
    struct BarBox
    {
        int bar = -1; // source bar, 0-based
        // System row on the page. Detected by the x-reset rule: bars run
        // left-to-right within a system, so a bar starting LEFT of its
        // predecessor opens the next row. No y-tolerance heuristics — those
        // merged tightly-spaced systems and let the unique-chunks wash spill
        // over neighbouring (new) material.
        int row = 0;
        glm::vec2 min{ 0.0f };
        glm::vec2 max{ 0.0f };
    };
    // A phrase's extent across one system row: line from x0..x1 at y, with
    // the phrase id + boundary confidence shown only on the first (opening)
    // segment.
    struct PhraseSpan
    {
        int phrase_id = -1;
        float x0 = 0.0f;
        float x1 = 0.0f;
        float y = 0.0f;
        bool opens_phrase = false; // first segment: draw the tick + label
        bool opens_section = false; // heavier tick — a section starts here
        float confidence = 0.0f;
        // Restatement: earliest phrase this one repeats (-1 = new material);
        // the label reads "P7 = P3" (literal) or "P7 ≈ P3" (transposed).
        int repeat_of = -1;
        bool transposed = false;
        // The system row's vertical extent (canvas space) — the unique-chunks
        // view washes repeated phrases over exactly this band.
        float row_y0 = 0.0f;
        float row_y1 = 0.0f;
    };
    struct Label
    {
        // Canvas-space ANCHOR: the bar's top edge for emphasis labels (drawn
        // above it) and its bottom edge otherwise (drawn below). The drawer
        // applies fixed pixel offsets — annotation text is UI-sized, never
        // proportional to the engraving.
        glm::vec2 pos{ 0.0f };
        std::string text;
        bool emphasis = false; // key names read larger than motif notes
    };
    struct Page
    {
        std::vector<BarBox> bars; // ascending bar
        std::vector<PhraseSpan> phrases;
        std::vector<Label> labels;
    };

    std::vector<Page> pages; // parallel to the engraved page list
    // Motif coloring: every note of every occurrence wears its motif's color
    // (the spelling palette's seven naturals, cycled by motif rank; a note in
    // two motifs keeps the stronger motif's color). One guidance state per
    // page, parallel to `pages` — the renderer applies it exactly like the
    // flow sheet's spelling colors.
    std::vector<ScoreHighlightState> highlights;
    // Banner legend, one entry per colored motif: swatch + count + interval
    // shape (semitones between successive melody notes).
    struct MotifLegend
    {
        int palette = 0; // kGuidancePalette index
        int count = 0;
        std::string shape; // "[+2 +2 +1 -5]"
    };
    std::vector<MotifLegend> motif_legend;
    std::string banner; // one-line summary for the viewport corner
    float staff_height = 0.0f; // canvas units; sizes derive from this

    bool empty() const
    {
        return pages.empty();
    }
};

// Builds the overlay for a paged engraving. `bar_starts_q` maps bar index ->
// start qstamp (ascending; one entry per bar) — the same axis the analysis
// ran on, so annotations land exactly where the profile's bars are.
// `midi_pitch`: sounding pitch for a note element id (-1 = unknown) — motif
// coloring must pick the TOP voice per onset, the same melody the analysis
// mined its motifs from.
AnalysisOverlay build_analysis_overlay(const std::vector<ScoreDrawList>& pages,
    const Timemap& timemap, const PieceProfile& profile,
    const std::vector<double>& bar_starts_q,
    const std::function<int(const std::string&)>& midi_pitch = {});

// Replays one page's annotations. origin/scale are the same mapping
// render_draw_list used for that page, so the green lands on the engraving.
// `unique_active`: the unique-chunks wash is also drawing, which carries its
// own centered "P11 = P1" — the lane label would duplicate it.
void draw_analysis_overlay(NVGcontext* vg, const AnalysisOverlay& overlay,
    const AnalysisOverlay::Page& page, glm::vec2 origin, float scale, float pixel_scale,
    const ScoreTextFonts& fonts, bool unique_active = false);

// The unique-chunks view ('s'): ghosts every restated phrase under a paper
// wash labelled with the phrase it repeats, so only NEW material reads at
// full strength — the piece's actual size, seen directly.
void draw_unique_wash(NVGcontext* vg, const AnalysisOverlay::Page& page, glm::vec2 origin,
    float scale, float pixel_scale, const ScoreTextFonts& fonts);

// The one-line summary, pinned to the viewport's top-left corner (screen
// space, independent of scroll).
void draw_analysis_banner(NVGcontext* vg, const AnalysisOverlay& overlay, float pixel_scale,
    const ScoreTextFonts& fonts);

} // namespace scoreview
} // namespace draxul

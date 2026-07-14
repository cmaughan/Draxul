#pragma once

#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>

struct NVGcontext;

namespace draxul
{
namespace scoreview
{

struct ScoreTextFonts
{
    int regular = -1;
    int italic = -1;
    int bold = -1;
    int music = -1; // SMuFL text face (Leipzig) for metronome-note runs
};

// Loads (once per NVGcontext) the serif faces used for score text — Verovio
// engraves text as "Times, serif". Missing variants fall back to regular; if
// no face is found at all the ids stay -1 and text runs are skipped.
ScoreTextFonts ensure_score_text_fonts(NVGcontext* vg);

// Replays one interpreted page into NanoVG. origin is the page's top-left in
// pixels; scale maps canvas units to pixels (stroke widths and font sizes are
// in canvas units and scale with the transform). When `highlight` is given,
// spelling colors and the wrong-note cross draw from its per-op state.
// `split_accidentals` toggles the half-color-over-black notehead cue for
// sharps/flats (off = accidentals wear their full spelling color, like the
// naturals).
void render_draw_list(NVGcontext* vg, const ScoreDrawList& list, glm::vec2 origin, float scale,
    const ScoreTextFonts& fonts, const ScoreHighlightState* highlight = nullptr,
    bool split_accidentals = true);

// White page sheet with a soft drop shadow; shared by the score renderer and
// the no-source placeholder scene.
void draw_page_sheet(NVGcontext* vg, float x, float y, float w, float h, float pixel_scale);

} // namespace scoreview
} // namespace draxul

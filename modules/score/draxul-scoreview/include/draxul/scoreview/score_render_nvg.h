#pragma once

#include <draxul/scoreview/score_draw_list.h>

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
};

// Loads (once per NVGcontext) the serif faces used for score text — Verovio
// engraves text as "Times, serif". Missing variants fall back to regular; if
// no face is found at all the ids stay -1 and text runs are skipped.
ScoreTextFonts ensure_score_text_fonts(NVGcontext* vg);

// Replays one interpreted page into NanoVG. origin is the page's top-left in
// pixels; scale maps canvas units to pixels (stroke widths and font sizes are
// in canvas units and scale with the transform).
void render_draw_list(NVGcontext* vg, const ScoreDrawList& list, glm::vec2 origin, float scale,
    const ScoreTextFonts& fonts);

// White page sheet with a soft drop shadow; shared by the score renderer and
// the no-source placeholder scene.
void draw_page_sheet(NVGcontext* vg, float x, float y, float w, float h, float pixel_scale);

} // namespace scoreview
} // namespace draxul

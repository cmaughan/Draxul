#pragma once

#include <draxul/scoreview/score_draw_list.h>

#include <optional>
#include <string>
#include <string_view>

namespace draxul
{
namespace scoreview
{

// Interprets one page of Verovio's constrained SVG dialect (as produced by
// VerovioLayoutEngine with svgViewBox enabled) into a ScoreDrawList. The
// dialect is machine-generated and narrow: <g>-grouped content with
// translate/scale transforms, glyph outlines in <defs> instanced via <use>,
// paths/polygons/polylines/rects/ellipses for primitives, and styled
// <text>/<tspan> runs. Unknown constructs are recorded in
// ScoreDrawList::warnings (once per kind) rather than dropped silently —
// the fixture tests treat any warning as version drift.
//
// Returns std::nullopt and fills `error` only for structural failures
// (unparseable XML, missing root/definition-scale svg).
std::optional<ScoreDrawList> interpret_score_svg(std::string_view svg_text, std::string& error);

// Exposed for unit testing: parses SVG path data (M L H V C S Z + relative
// forms, implicit repetition) into path commands. Returns false on a malformed
// command letter or truncated coordinates.
bool parse_svg_path_data(std::string_view data, std::vector<PathCmd>& out);

} // namespace scoreview
} // namespace draxul

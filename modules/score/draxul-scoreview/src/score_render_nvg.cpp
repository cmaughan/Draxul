#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/score_render_nvg.h>

#include <draxul/runtime_path.h>

#include "nanovg.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace draxul
{
namespace scoreview
{

namespace
{

const NVGcolor INK = { { { 0.10f, 0.09f, 0.08f, 1.0f } } };
const NVGcolor PAGE_WHITE = { { { 0.988f, 0.984f, 0.972f, 1.0f } } };
// The clock-conveyor "passed" tint (burnt amber) and the wrong-note cross
// (red). A right/wrong verdict no longer recolors the note — the spelling
// color stays and a wrong note gets a red cross drawn over its head instead.
const NVGcolor ACCENT = { { { 0.85f, 0.45f, 0.08f, 1.0f } } };
const NVGcolor CROSS_RED = { { { 0.82f, 0.11f, 0.09f, 1.0f } } };

// A glyph is a notehead when its SMuFL codepoint (the hex before the '-' in
// the symbol id) is in the Noteheads block U+E0A0-E0FF — accidentals, stems,
// and flags fall outside it, so only the note's head splits.
bool is_notehead(const std::string& symbol_id)
{
    const long cp = std::strtol(symbol_id.c_str(), nullptr, 16);
    return cp >= 0xE0A0 && cp <= 0xE0FF;
}

int create_font_from_candidates(NVGcontext* vg, const char* name,
    const std::array<const char*, 4>& candidates)
{
    for (const char* path : candidates)
    {
        if (path == nullptr)
            continue;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            continue;
        const int font = nvgCreateFont(vg, name, path);
        if (font >= 0)
            return font;
    }
    return -1;
}

void replay_commands(NVGcontext* vg, const std::vector<PathCmd>& cmds)
{
    for (const PathCmd& cmd : cmds)
    {
        switch (cmd.op)
        {
        case PathCmd::Op::MoveTo:
            nvgMoveTo(vg, cmd.p.x, cmd.p.y);
            // Glyph counters (classified at interpretation time) must be
            // marked explicitly — NanoVG renders every subpath solid otherwise.
            if (cmd.hole)
                nvgPathWinding(vg, NVG_HOLE);
            break;
        case PathCmd::Op::LineTo:
            nvgLineTo(vg, cmd.p.x, cmd.p.y);
            break;
        case PathCmd::Op::CubicTo:
            nvgBezierTo(vg, cmd.c1.x, cmd.c1.y, cmd.c2.x, cmd.c2.y, cmd.p.x, cmd.p.y);
            break;
        case PathCmd::Op::Close:
            nvgClosePath(vg);
            break;
        }
    }
}

} // namespace

ScoreTextFonts ensure_score_text_fonts(NVGcontext* vg)
{
    ScoreTextFonts fonts;
    fonts.regular = nvgFindFont(vg, "score-serif");
    if (fonts.regular < 0)
    {
#ifdef __APPLE__
        fonts.regular = create_font_from_candidates(vg, "score-serif",
            { "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
                "/System/Library/Fonts/Supplemental/Georgia.ttf", nullptr, nullptr });
        fonts.italic = create_font_from_candidates(vg, "score-serif-italic",
            { "/System/Library/Fonts/Supplemental/Times New Roman Italic.ttf",
                "/System/Library/Fonts/Supplemental/Georgia Italic.ttf", nullptr, nullptr });
        fonts.bold = create_font_from_candidates(vg, "score-serif-bold",
            { "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
                "/System/Library/Fonts/Supplemental/Georgia Bold.ttf", nullptr, nullptr });
#else
        fonts.regular = create_font_from_candidates(vg, "score-serif",
            { "C:/Windows/Fonts/times.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
                nullptr, nullptr });
        fonts.italic = create_font_from_candidates(vg, "score-serif-italic",
            { "C:/Windows/Fonts/timesi.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf", nullptr, nullptr });
        fonts.bold = create_font_from_candidates(vg, "score-serif-bold",
            { "C:/Windows/Fonts/timesbd.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", nullptr, nullptr });
#endif
    }
    else
    {
        fonts.italic = nvgFindFont(vg, "score-serif-italic");
        fonts.bold = nvgFindFont(vg, "score-serif-bold");
    }
    if (fonts.italic < 0)
        fonts.italic = fonts.regular;
    if (fonts.bold < 0)
        fonts.bold = fonts.regular;

    // SMuFL music text face (metronome notes in tempo marks). Staged next to
    // the executable with the Verovio resources; no serif fallback — its
    // codepoints are Private Use Area and would render as .notdef boxes.
    fonts.music = nvgFindFont(vg, "score-music");
    if (fonts.music < 0)
    {
        const std::string leipzig = (executable_directory() / "verovio-data" / "Leipzig.ttf").string();
        fonts.music = create_font_from_candidates(
            vg, "score-music", { leipzig.c_str(), nullptr, nullptr, nullptr });
    }
    return fonts;
}

void render_draw_list(NVGcontext* vg, const ScoreDrawList& list, glm::vec2 origin, float scale,
    const ScoreTextFonts& fonts, const ScoreHighlightState* highlight, bool split_accidentals)
{
    nvgSave(vg);
    nvgTranslate(vg, origin.x, origin.y);
    nvgScale(vg, scale, scale);

    const auto state_color = [highlight](const std::vector<uint8_t>& states,
                                 const std::vector<uint8_t>& guides, size_t i) -> NVGcolor {
        const uint8_t state = (highlight != nullptr && i < states.size()) ? states[i] : 0;
        // Only the clock-mode conveyor tints (amber). A right/wrong verdict no
        // longer recolors — the note keeps its spelling color and a wrong note
        // gets a cross drawn over it (below).
        if (state == static_cast<uint8_t>(ScoreHighlightState::State::Passed))
            return ACCENT;
        if (highlight != nullptr && i < guides.size() && guides[i] > 0)
        {
            const unsigned char* tint = kGuidancePalette[(guides[i] - 1) % kGuidancePaletteSize];
            return nvgRGBA(tint[0], tint[1], tint[2], 255);
        }
        return INK;
    };

    for (size_t i = 0; i < list.paths.size(); ++i)
    {
        const DrawPath& path = list.paths[i];
        const NVGcolor color = highlight != nullptr
            ? state_color(highlight->path_lit, highlight->path_guide, i)
            : INK;
        nvgBeginPath(vg);
        replay_commands(vg, path.cmds);
        if (path.fill)
        {
            nvgFillColor(vg, color);
            nvgFill(vg);
        }
        if (path.stroke_width > 0.0f)
        {
            nvgStrokeColor(vg, color);
            nvgStrokeWidth(vg, path.stroke_width);
            nvgLineCap(vg, NVG_BUTT);
            nvgLineJoin(vg, NVG_MITER);
            nvgStroke(vg);
        }
    }

    for (size_t i = 0; i < list.glyphs.size(); ++i)
    {
        const GlyphInstance& glyph = list.glyphs[i];
        if (glyph.symbol_index < 0 || glyph.symbol_index >= static_cast<int>(list.symbols.size()))
            continue;
        const SymbolOutline& symbol = list.symbols[glyph.symbol_index];
        if (symbol.cmds.empty())
            continue;
        const Affine& m = glyph.xform;
        const NVGcolor color = highlight != nullptr
            ? state_color(highlight->glyph_lit, highlight->glyph_guide, i)
            : INK;
        const int guide = (highlight != nullptr && i < highlight->glyph_guide.size())
            ? highlight->glyph_guide[i]
            : 0;
        const int sign = guide > 0 ? (guide - 1) % 3 : 1; // 0 flat, 1 nat, 2 sharp
        const uint8_t state = (highlight != nullptr && i < highlight->glyph_lit.size())
            ? highlight->glyph_lit[i]
            : 0;
        const bool notehead = is_notehead(symbol.id);
        const bool split = notehead && split_accidentals && (sign == 0 || sign == 2);
        const bool wrong
            = notehead && state == static_cast<uint8_t>(ScoreHighlightState::State::Missed);

        // The head's canvas-space bounding box (its font-unit extent mapped
        // through the glyph affine) — the accidental split and the cross both
        // need it, so compute it once when either applies.
        glm::vec2 cmin{ 1e30f, 1e30f };
        glm::vec2 cmax{ -1e30f, -1e30f };
        if (split || wrong)
        {
            glm::vec2 lo{ 1e30f, 1e30f };
            glm::vec2 hi{ -1e30f, -1e30f };
            for (const PathCmd& cmd : symbol.cmds)
            {
                lo = glm::min(lo, cmd.p);
                hi = glm::max(hi, cmd.p);
                if (cmd.op == PathCmd::Op::CubicTo)
                {
                    lo = glm::min(glm::min(lo, cmd.c1), cmd.c2);
                    hi = glm::max(glm::max(hi, cmd.c1), cmd.c2);
                }
            }
            for (const glm::vec2 corner :
                { glm::vec2{ lo.x, lo.y }, glm::vec2{ hi.x, lo.y }, glm::vec2{ lo.x, hi.y },
                    glm::vec2{ hi.x, hi.y } })
            {
                const glm::vec2 q = m.apply(corner);
                cmin = glm::min(cmin, q);
                cmax = glm::max(cmax, q);
            }
        }

        if (split)
        {
            // An accidental's notehead is a spelling cue: the standard black
            // notehead first (hole preserved for minims — the note and its
            // timing stay legible), then the spelling color over one VERTICAL
            // half — a sharp the right, a flat the left. Left/right (not
            // top/bottom) keeps the note's full height, so its level on the
            // stave stays clear.
            const float mid = (cmin.x + cmax.x) * 0.5f;
            const bool sharp = sign == 2;
            nvgSave(vg);
            nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
            nvgBeginPath(vg);
            replay_commands(vg, symbol.cmds);
            nvgFillColor(vg, INK);
            nvgFill(vg);
            nvgRestore(vg);
            nvgSave(vg);
            nvgIntersectScissor(
                vg, sharp ? mid : cmin.x, cmin.y, mid - cmin.x, cmax.y - cmin.y);
            nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
            nvgBeginPath(vg);
            replay_commands(vg, symbol.cmds);
            nvgFillColor(vg, color);
            nvgFill(vg);
            nvgRestore(vg);
        }
        else
        {
            nvgSave(vg);
            nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
            nvgBeginPath(vg);
            replay_commands(vg, symbol.cmds);
            nvgFillColor(vg, color);
            nvgFill(vg);
            nvgRestore(vg);
        }

        // A wrong note is flagged with a small red cross over its head — the
        // note keeps its own color, only the cross marks the mistake. A white
        // halo keeps it visible over any note color.
        if (wrong)
        {
            const float cx = (cmin.x + cmax.x) * 0.5f;
            const float cy = (cmin.y + cmax.y) * 0.5f;
            const float span = glm::max(cmax.x - cmin.x, cmax.y - cmin.y);
            const float r = 0.28f * span; // about half the head — a compact mark
            const float sw = 0.12f * span;
            const auto stroke_cross = [&](NVGcolor c, float w) {
                nvgStrokeColor(vg, c);
                nvgStrokeWidth(vg, w);
                nvgLineCap(vg, NVG_ROUND);
                nvgBeginPath(vg);
                nvgMoveTo(vg, cx - r, cy - r);
                nvgLineTo(vg, cx + r, cy + r);
                nvgMoveTo(vg, cx - r, cy + r);
                nvgLineTo(vg, cx + r, cy - r);
                nvgStroke(vg);
            };
            stroke_cross(nvgRGBA(255, 255, 255, 235), sw * 1.9f);
            stroke_cross(CROSS_RED, sw);
        }
    }

    if (fonts.regular >= 0)
    {
        // Inline chaining: runs flagged continues_previous flow after the
        // measured end of the run before them (tspans of one <text> share an
        // anchor in the SVG; drawing them all there would overdraw). The pad
        // stands in for the boundary spaces collapse_whitespace strips.
        float chain_x = 0.0f;
        bool chain_valid = false;
        for (const DrawText& text : list.texts)
        {
            int face = fonts.regular;
            if (text.music_font)
                face = fonts.music;
            else if (text.bold)
                face = fonts.bold;
            else if (text.italic)
                face = fonts.italic;
            if (face < 0)
            {
                chain_valid = false; // unmeasurable — followers fall back to pos
                continue;
            }
            nvgFontFaceId(vg, face);
            nvgFontSize(vg, text.font_size);
            float x = text.pos.x;
            int align = NVG_ALIGN_BASELINE;
            if (text.continues_previous && chain_valid)
            {
                x = chain_x + text.font_size * 0.27f;
                align |= NVG_ALIGN_LEFT;
            }
            else
            {
                switch (text.anchor)
                {
                case DrawText::Anchor::Start:
                    align |= NVG_ALIGN_LEFT;
                    break;
                case DrawText::Anchor::Middle:
                    align |= NVG_ALIGN_CENTER;
                    break;
                case DrawText::Anchor::End:
                    align |= NVG_ALIGN_RIGHT;
                    break;
                }
            }
            nvgTextAlign(vg, align);
            nvgFillColor(vg, INK);
            chain_x = nvgText(vg, x, text.pos.y, text.content.c_str(), nullptr);
            chain_valid = true;
        }
    }

    nvgRestore(vg);
}

void draw_page_sheet(NVGcontext* vg, float x, float y, float w, float h, float pixel_scale)
{
    const float corner = 2.0f * pixel_scale;
    NVGpaint shadow = nvgBoxGradient(vg, x, y + 3.0f * pixel_scale, w, h, corner * 2.0f,
        14.0f * pixel_scale, nvgRGBA(0, 0, 0, 80), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x - 24.0f * pixel_scale, y - 24.0f * pixel_scale, w + 48.0f * pixel_scale,
        h + 48.0f * pixel_scale);
    nvgRoundedRect(vg, x, y, w, h, corner);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, corner);
    nvgFillColor(vg, PAGE_WHITE);
    nvgFill(vg);
}

} // namespace scoreview
} // namespace draxul

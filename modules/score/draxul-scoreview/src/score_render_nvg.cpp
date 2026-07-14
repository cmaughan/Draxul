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
// Highlight palette indexed by ScoreHighlightState::State:
// None=ink, Passed=burnt amber, Correct=green, Missed=red — all chosen for
// contrast against both the page white and each other.
const NVGcolor ACCENT = { { { 0.85f, 0.45f, 0.08f, 1.0f } } };
const NVGcolor CORRECT_GREEN = { { { 0.13f, 0.55f, 0.22f, 1.0f } } };
const NVGcolor MISS_RED = { { { 0.80f, 0.13f, 0.10f, 1.0f } } };
const NVGcolor STATE_COLORS[4] = { INK, ACCENT, CORRECT_GREEN, MISS_RED };

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
    const ScoreTextFonts& fonts, const ScoreHighlightState* highlight)
{
    nvgSave(vg);
    nvgTranslate(vg, origin.x, origin.y);
    nvgScale(vg, scale, scale);

    const auto state_color = [highlight](const std::vector<uint8_t>& states,
                                 const std::vector<uint8_t>& guides, size_t i) -> NVGcolor {
        const uint8_t state = (highlight != nullptr && i < states.size()) ? states[i] : 0;
        if (state == 0 && highlight != nullptr && i < guides.size() && guides[i] > 0)
        {
            // The keyboard pairing: an upcoming note wears its key's color.
            const unsigned char* tint = kGuidancePalette[(guides[i] - 1) % kGuidancePaletteSize];
            return nvgRGBA(tint[0], tint[1], tint[2], 255);
        }
        return STATE_COLORS[state < 4 ? state : 0];
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

        // An accidental's notehead renders as a HALF-filled circle — a spelling
        // cue on the note itself: a sharp fills the TOP half (bottom white), a
        // flat the BOTTOM half (top white). The spelling comes from the guide
        // palette index (letter*3 + sign+1): sign 0 = flat, 2 = sharp.
        const int guide = (highlight != nullptr && i < highlight->glyph_guide.size())
            ? highlight->glyph_guide[i]
            : 0;
        const int sign = guide > 0 ? (guide - 1) % 3 : 1;
        if ((sign == 0 || sign == 2) && is_notehead(symbol.id))
        {
            // Canvas-space vertical extent of the head (its font-unit bounds
            // mapped through the glyph's affine) gives the split line.
            glm::vec2 lo{ 1e30f, 1e30f };
            glm::vec2 hi{ -1e30f, -1e30f };
            const auto grow = [&](glm::vec2 p) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            };
            for (const PathCmd& cmd : symbol.cmds)
            {
                grow(cmd.p);
                if (cmd.op == PathCmd::Op::CubicTo)
                {
                    grow(cmd.c1);
                    grow(cmd.c2);
                }
            }
            glm::vec2 cmin{ 1e30f, 1e30f };
            glm::vec2 cmax{ -1e30f, -1e30f };
            for (const glm::vec2 corner :
                { glm::vec2{ lo.x, lo.y }, glm::vec2{ hi.x, lo.y }, glm::vec2{ lo.x, hi.y },
                    glm::vec2{ hi.x, hi.y } })
            {
                const glm::vec2 q = m.apply(corner);
                cmin = glm::min(cmin, q);
                cmax = glm::max(cmax, q);
            }
            const float mid = (cmin.y + cmax.y) * 0.5f;
            const NVGcolor white = nvgRGBA(255, 255, 255, 255);
            const bool sharp = sign == 2;
            const auto draw_half = [&](float y0, float y1, NVGcolor fill) {
                nvgSave(vg);
                nvgIntersectScissor(vg, cmin.x, y0, cmax.x - cmin.x, y1 - y0);
                nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
                nvgBeginPath(vg);
                replay_commands(vg, symbol.cmds);
                nvgFillColor(vg, fill);
                nvgFill(vg);
                nvgRestore(vg);
            };
            draw_half(cmin.y, mid, sharp ? color : white); // top
            draw_half(mid, cmax.y, sharp ? white : color); // bottom
            continue;
        }

        nvgSave(vg);
        nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
        nvgBeginPath(vg);
        replay_commands(vg, symbol.cmds);
        nvgFillColor(vg, color);
        nvgFill(vg);
        nvgRestore(vg);
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

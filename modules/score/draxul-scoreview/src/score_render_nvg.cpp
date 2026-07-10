#include <draxul/scoreview/score_render_nvg.h>

#include "nanovg.h"

#include <array>
#include <filesystem>

namespace draxul
{
namespace scoreview
{

namespace
{

const NVGcolor INK = { { { 0.10f, 0.09f, 0.08f, 1.0f } } };
const NVGcolor PAGE_WHITE = { { { 0.988f, 0.984f, 0.972f, 1.0f } } };

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
    return fonts;
}

void render_draw_list(NVGcontext* vg, const ScoreDrawList& list, glm::vec2 origin, float scale,
    const ScoreTextFonts& fonts)
{
    nvgSave(vg);
    nvgTranslate(vg, origin.x, origin.y);
    nvgScale(vg, scale, scale);

    for (const DrawPath& path : list.paths)
    {
        nvgBeginPath(vg);
        replay_commands(vg, path.cmds);
        if (path.fill)
        {
            nvgFillColor(vg, INK);
            nvgFill(vg);
        }
        if (path.stroke_width > 0.0f)
        {
            nvgStrokeColor(vg, INK);
            nvgStrokeWidth(vg, path.stroke_width);
            nvgLineCap(vg, NVG_BUTT);
            nvgLineJoin(vg, NVG_MITER);
            nvgStroke(vg);
        }
    }

    for (const GlyphInstance& glyph : list.glyphs)
    {
        if (glyph.symbol_index < 0 || glyph.symbol_index >= static_cast<int>(list.symbols.size()))
            continue;
        const SymbolOutline& symbol = list.symbols[glyph.symbol_index];
        if (symbol.cmds.empty())
            continue;
        nvgSave(vg);
        const Affine& m = glyph.xform;
        nvgTransform(vg, m.a, m.b, m.c, m.d, m.e, m.f);
        nvgBeginPath(vg);
        replay_commands(vg, symbol.cmds);
        nvgFillColor(vg, INK);
        nvgFill(vg);
        nvgRestore(vg);
    }

    if (fonts.regular >= 0)
    {
        for (const DrawText& text : list.texts)
        {
            int face = fonts.regular;
            if (text.bold)
                face = fonts.bold;
            else if (text.italic)
                face = fonts.italic;
            nvgFontFaceId(vg, face);
            nvgFontSize(vg, text.font_size);
            int align = NVG_ALIGN_BASELINE;
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
            nvgTextAlign(vg, align);
            nvgFillColor(vg, INK);
            nvgText(vg, text.pos.x, text.pos.y, text.content.c_str(), nullptr);
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

#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Resolution-independent draw list produced by interpreting Verovio's SVG
// dialect (plans/scoreview.md phase 3). Coordinates live in the SVG
// "definition-scale" user space (canvas_size extent); the renderer maps that
// space to screen pixels with a single uniform transform. Every op carries
// the id of its nearest identified ancestor element — the future hook for
// hit-testing and playback highlighting.

namespace draxul
{
namespace scoreview
{

// SVG-style 2x3 affine: x' = a*x + c*y + e, y' = b*x + d*y + f.
struct Affine
{
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

    glm::vec2 apply(glm::vec2 p) const
    {
        return { a * p.x + c * p.y + e, b * p.x + d * p.y + f };
    }

    // Composition where `child` applies first: (this ∘ child)(p).
    Affine then(const Affine& o) const
    {
        Affine r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e;
        r.f = b * o.e + d * o.f + f;
        return r;
    }

    static Affine translate(float x, float y)
    {
        Affine r;
        r.e = x;
        r.f = y;
        return r;
    }
    static Affine scale(float sx, float sy)
    {
        Affine r;
        r.a = sx;
        r.d = sy;
        return r;
    }

    float uniform_scale() const
    {
        return std::sqrt(std::abs(a * d - b * c));
    }
};

struct PathCmd
{
    enum class Op : uint8_t
    {
        MoveTo, // p
        LineTo, // p
        CubicTo, // c1, c2, p
        Close,
    };
    Op op = Op::MoveTo;
    glm::vec2 p{ 0.0f };
    glm::vec2 c1{ 0.0f };
    glm::vec2 c2{ 0.0f };
};

// A glyph outline from the SVG <defs>, in font units with Verovio's
// scale(1,-1) y-flip already baked into the stored commands.
struct SymbolOutline
{
    std::string id; // e.g. "E050-xxxx" — leading codepoint is the SMuFL glyph
    std::vector<PathCmd> cmds;
};

struct GlyphInstance
{
    int symbol_index = -1; // into ScoreDrawList::symbols
    Affine xform; // font units -> canvas space (ancestors composed in)
    std::string element_id;
};

// General path content (staff lines, stems, barlines, beams, slurs, ties,
// hairpins, dots, ...). Coordinates are pre-transformed into canvas space.
struct DrawPath
{
    std::vector<PathCmd> cmds;
    bool fill = false;
    float stroke_width = 0.0f; // 0 = no stroke; already in canvas units
    std::string element_id;
};

struct DrawText
{
    enum class Anchor : uint8_t
    {
        Start,
        Middle,
        End,
    };
    glm::vec2 pos{ 0.0f }; // baseline position in canvas space
    float font_size = 0.0f;
    Anchor anchor = Anchor::Start;
    bool italic = false;
    bool bold = false;
    std::string content;
    std::string element_id;
};

struct ScoreDrawList
{
    std::vector<SymbolOutline> symbols;
    std::vector<GlyphInstance> glyphs;
    std::vector<DrawPath> paths;
    std::vector<DrawText> texts;

    glm::vec2 canvas_size{ 0.0f }; // definition-scale viewBox extent
    glm::vec2 pixel_size{ 0.0f }; // outer svg viewBox extent (engine's px intent)

    // One entry per unhandled element/construct kind; empty = full coverage.
    std::vector<std::string> warnings;

    bool empty() const
    {
        return glyphs.empty() && paths.empty() && texts.empty();
    }
};

} // namespace scoreview
} // namespace draxul

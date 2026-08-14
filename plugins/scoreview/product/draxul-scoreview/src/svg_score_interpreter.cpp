#include <draxul/scoreview/svg_score_interpreter.h>

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

namespace draxul
{
namespace scoreview
{

namespace
{

using tinyxml2::XMLElement;
using tinyxml2::XMLNode;
using tinyxml2::XMLText;

// ---------------------------------------------------------------------------
// Small scanners
// ---------------------------------------------------------------------------

// Reads whitespace/comma-separated floats: "1375, 1080" / "0.72 0.72".
std::vector<float> parse_number_list(std::string_view text)
{
    std::vector<float> values;
    const char* cursor = text.data();
    const char* end = text.data() + text.size();
    while (cursor < end)
    {
        while (cursor < end && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ',' || *cursor == '(' || *cursor == ')'))
            ++cursor;
        if (cursor >= end)
            break;
        char* after = nullptr;
        const float value = std::strtof(cursor, &after);
        if (after == cursor)
            break;
        values.push_back(value);
        cursor = after;
    }
    return values;
}

// Parses an SVG transform attribute: a sequence of translate(..)/scale(..)/
// matrix(..) applied left-to-right (leftmost outermost).
bool parse_transform(std::string_view text, Affine& out)
{
    Affine result;
    size_t pos = 0;
    while (pos < text.size())
    {
        while (pos < text.size() && (std::isspace(static_cast<unsigned char>(text[pos])) || text[pos] == ','))
            ++pos;
        if (pos >= text.size())
            break;
        const size_t open = text.find('(', pos);
        if (open == std::string_view::npos)
            return false;
        const size_t close = text.find(')', open);
        if (close == std::string_view::npos)
            return false;
        std::string_view name = text.substr(pos, open - pos);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.remove_suffix(1);
        const std::vector<float> args = parse_number_list(text.substr(open + 1, close - open - 1));

        Affine op;
        if (name == "translate" && !args.empty())
            op = Affine::translate(args[0], args.size() > 1 ? args[1] : 0.0f);
        else if (name == "scale" && !args.empty())
            op = Affine::scale(args[0], args.size() > 1 ? args[1] : args[0]);
        else if (name == "matrix" && args.size() == 6)
        {
            op.a = args[0];
            op.b = args[1];
            op.c = args[2];
            op.d = args[3];
            op.e = args[4];
            op.f = args[5];
        }
        else
            return false;
        result = result.then(op);
        pos = close + 1;
    }
    out = result;
    return true;
}

std::string collapse_whitespace(std::string_view text)
{
    std::string out;
    bool pending_space = false;
    for (char ch : text)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space)
        {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(ch);
    }
    return out;
}

// Marks hole subpaths for nonzero-rule rendering: a contour winding opposite
// to the dominant (largest-area) contour is a counter — the gap in a treble
// clef, the bowl of a 'p'. NanoVG fills every subpath as solid unless told
// otherwise, so the classification is baked into MoveTo commands once here.
// The shoelace sum runs over control polygons, whose orientation matches the
// curves they bound.
void classify_hole_subpaths(std::vector<PathCmd>& cmds)
{
    struct Subpath
    {
        size_t move_index = 0;
        std::vector<glm::vec2> points;
    };
    std::vector<Subpath> subpaths;
    for (size_t i = 0; i < cmds.size(); ++i)
    {
        const PathCmd& cmd = cmds[i];
        if (cmd.op == PathCmd::Op::MoveTo)
        {
            subpaths.push_back({ i, { cmd.p } });
            continue;
        }
        if (subpaths.empty())
            continue;
        if (cmd.op == PathCmd::Op::LineTo)
            subpaths.back().points.push_back(cmd.p);
        else if (cmd.op == PathCmd::Op::CubicTo)
        {
            subpaths.back().points.push_back(cmd.c1);
            subpaths.back().points.push_back(cmd.c2);
            subpaths.back().points.push_back(cmd.p);
        }
    }
    if (subpaths.size() < 2)
        return;

    std::vector<float> areas(subpaths.size(), 0.0f);
    float dominant = 0.0f;
    for (size_t s = 0; s < subpaths.size(); ++s)
    {
        const std::vector<glm::vec2>& pts = subpaths[s].points;
        float doubled = 0.0f;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const glm::vec2& a = pts[i];
            const glm::vec2& b = pts[(i + 1) % pts.size()];
            doubled += a.x * b.y - b.x * a.y;
        }
        areas[s] = doubled;
        if (std::abs(doubled) > std::abs(dominant))
            dominant = doubled;
    }
    if (dominant == 0.0f)
        return;
    for (size_t s = 0; s < subpaths.size(); ++s)
        cmds[subpaths[s].move_index].hole = areas[s] * dominant < 0.0f;
}

// SMuFL music text fonts Verovio may name on tspans (metronome-note glyphs in
// tempo marks etc.). Their codepoints live in the Private Use Area — pushing
// them through a serif face renders .notdef boxes.
bool is_music_font(const char* family)
{
    static constexpr const char* MUSIC_FONTS[] = { "Leipzig", "VerovioText", "Bravura",
        "Petaluma", "Gootville", "Leland" };
    for (const char* name : MUSIC_FONTS)
    {
        if (std::strstr(family, name) != nullptr)
            return true;
    }
    return false;
}

// Ellipse as four cubic arcs (standard circle-approximation constant).
void append_ellipse(std::vector<PathCmd>& cmds, glm::vec2 center, glm::vec2 radii)
{
    constexpr float K = 0.5522847f;
    const float kx = radii.x * K;
    const float ky = radii.y * K;
    const glm::vec2 c = center;
    const glm::vec2 r = radii;
    cmds.push_back({ PathCmd::Op::MoveTo, { c.x + r.x, c.y } });
    cmds.push_back(
        { PathCmd::Op::CubicTo, { c.x, c.y + r.y }, { c.x + r.x, c.y + ky }, { c.x + kx, c.y + r.y } });
    cmds.push_back(
        { PathCmd::Op::CubicTo, { c.x - r.x, c.y }, { c.x - kx, c.y + r.y }, { c.x - r.x, c.y + ky } });
    cmds.push_back(
        { PathCmd::Op::CubicTo, { c.x, c.y - r.y }, { c.x - r.x, c.y - ky }, { c.x - kx, c.y - r.y } });
    cmds.push_back(
        { PathCmd::Op::CubicTo, { c.x + r.x, c.y }, { c.x + kx, c.y - r.y }, { c.x + r.x, c.y - ky } });
    cmds.push_back({ PathCmd::Op::Close });
}

} // namespace

// ---------------------------------------------------------------------------
// Path data parser
// ---------------------------------------------------------------------------

bool parse_svg_path_data(std::string_view data, std::vector<PathCmd>& out)
{
    const char* cursor = data.data();
    const char* end = data.data() + data.size();

    glm::vec2 current{ 0.0f };
    glm::vec2 subpath_start{ 0.0f };
    glm::vec2 last_cubic_c2{ 0.0f };
    bool last_was_cubic = false;
    char command = 0;

    auto skip_separators = [&]() {
        while (cursor < end && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ','))
            ++cursor;
    };
    auto read_number = [&](float& value) -> bool {
        skip_separators();
        char* after = nullptr;
        value = std::strtof(cursor, &after);
        if (after == cursor)
            return false;
        cursor = after;
        return true;
    };
    auto read_point = [&](glm::vec2& p) -> bool { return read_number(p.x) && read_number(p.y); };

    while (true)
    {
        skip_separators();
        if (cursor >= end)
            break;
        if (std::isalpha(static_cast<unsigned char>(*cursor)))
            command = *cursor++;
        else if (command == 0)
            return false;
        // Implicit repetition: a MoveTo repeats as LineTo.
        else if (command == 'M')
            command = 'L';
        else if (command == 'm')
            command = 'l';

        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        bool cubic = false;
        switch (std::toupper(static_cast<unsigned char>(command)))
        {
        case 'M':
        {
            glm::vec2 p;
            if (!read_point(p))
                return false;
            if (relative)
                p += current;
            out.push_back({ PathCmd::Op::MoveTo, p });
            current = p;
            subpath_start = p;
            break;
        }
        case 'L':
        {
            glm::vec2 p;
            if (!read_point(p))
                return false;
            if (relative)
                p += current;
            out.push_back({ PathCmd::Op::LineTo, p });
            current = p;
            break;
        }
        case 'H':
        {
            float x = 0.0f;
            if (!read_number(x))
                return false;
            glm::vec2 p{ relative ? current.x + x : x, current.y };
            out.push_back({ PathCmd::Op::LineTo, p });
            current = p;
            break;
        }
        case 'V':
        {
            float y = 0.0f;
            if (!read_number(y))
                return false;
            glm::vec2 p{ current.x, relative ? current.y + y : y };
            out.push_back({ PathCmd::Op::LineTo, p });
            current = p;
            break;
        }
        case 'C':
        {
            glm::vec2 c1, c2, p;
            if (!read_point(c1) || !read_point(c2) || !read_point(p))
                return false;
            if (relative)
            {
                c1 += current;
                c2 += current;
                p += current;
            }
            out.push_back({ PathCmd::Op::CubicTo, p, c1, c2 });
            last_cubic_c2 = c2;
            current = p;
            cubic = true;
            break;
        }
        case 'S':
        {
            glm::vec2 c2, p;
            if (!read_point(c2) || !read_point(p))
                return false;
            if (relative)
            {
                c2 += current;
                p += current;
            }
            const glm::vec2 c1 = last_was_cubic ? current * 2.0f - last_cubic_c2 : current;
            out.push_back({ PathCmd::Op::CubicTo, p, c1, c2 });
            last_cubic_c2 = c2;
            current = p;
            cubic = true;
            break;
        }
        case 'Z':
        {
            out.push_back({ PathCmd::Op::Close });
            current = subpath_start;
            break;
        }
        default:
            return false; // Q/A/T not emitted by Verovio; fail loudly if they appear
        }
        last_was_cubic = cubic;
    }
    return true;
}

namespace
{

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------

class Interpreter
{
public:
    std::optional<ScoreDrawList> run(std::string_view svg_text, std::string& error)
    {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(svg_text.data(), svg_text.size()) != tinyxml2::XML_SUCCESS)
        {
            error = std::string("SVG parse error: ") + doc.ErrorStr();
            return std::nullopt;
        }
        const XMLElement* root = doc.RootElement();
        if (root == nullptr || std::strcmp(root->Name(), "svg") != 0)
        {
            error = "SVG parse error: missing <svg> root";
            return std::nullopt;
        }
        read_view_box(root, list_.pixel_size);

        if (const XMLElement* defs = root->FirstChildElement("defs"))
            parse_defs(defs);

        const XMLElement* content = nullptr;
        for (const XMLElement* child = root->FirstChildElement("svg"); child != nullptr;
            child = child->NextSiblingElement("svg"))
        {
            const char* cls = child->Attribute("class");
            if (cls != nullptr && std::strstr(cls, "definition-scale") != nullptr)
            {
                content = child;
                break;
            }
        }
        if (content == nullptr)
        {
            error = "SVG parse error: no definition-scale svg element";
            return std::nullopt;
        }
        read_view_box(content, list_.canvas_size);

        Context ctx;
        walk_children(content, ctx);

        for (const std::string& warning : warnings_)
            list_.warnings.push_back(warning);
        return std::move(list_);
    }

private:
    struct Context
    {
        Affine xform;
        std::string element_id;
        int measure = -1; // index into list_.measures while inside one
        bool italic = false;
        bool bold = false;
    };

    static void read_view_box(const XMLElement* element, glm::vec2& out_size)
    {
        const char* view_box = element->Attribute("viewBox");
        if (view_box == nullptr)
            return;
        const std::vector<float> values = parse_number_list(view_box);
        if (values.size() == 4)
            out_size = { values[2], values[3] };
    }

    void warn(const std::string& message)
    {
        warnings_.insert(message);
    }

    void parse_defs(const XMLElement* defs)
    {
        for (const XMLElement* group = defs->FirstChildElement("g"); group != nullptr;
            group = group->NextSiblingElement("g"))
        {
            const char* id = group->Attribute("id");
            if (id == nullptr)
                continue;
            SymbolOutline symbol;
            symbol.id = id;
            for (const XMLElement* path = group->FirstChildElement("path"); path != nullptr;
                path = path->NextSiblingElement("path"))
            {
                Affine xform; // typically scale(1,-1): bake the y-flip into the outline
                if (const char* transform = path->Attribute("transform"))
                {
                    if (!parse_transform(transform, xform))
                        warn("unhandled transform in <defs>: " + std::string(transform));
                }
                std::vector<PathCmd> cmds;
                const char* d = path->Attribute("d");
                if (d == nullptr || !parse_svg_path_data(d, cmds))
                {
                    warn("unparseable glyph path in <defs> symbol " + symbol.id);
                    continue;
                }
                for (PathCmd& cmd : cmds)
                {
                    cmd.p = xform.apply(cmd.p);
                    cmd.c1 = xform.apply(cmd.c1);
                    cmd.c2 = xform.apply(cmd.c2);
                }
                symbol.cmds.insert(symbol.cmds.end(), cmds.begin(), cmds.end());
            }
            classify_hole_subpaths(symbol.cmds);
            symbol_index_.emplace(symbol.id, static_cast<int>(list_.symbols.size()));
            list_.symbols.push_back(std::move(symbol));
        }
    }

    Affine element_transform(const XMLElement* element, const Context& ctx)
    {
        const char* transform = element->Attribute("transform");
        if (transform == nullptr)
            return ctx.xform;
        Affine local;
        if (!parse_transform(transform, local))
        {
            warn("unhandled transform: " + std::string(transform));
            return ctx.xform;
        }
        return ctx.xform.then(local);
    }

    void walk_children(const XMLElement* parent, const Context& ctx)
    {
        for (const XMLElement* child = parent->FirstChildElement(); child != nullptr;
            child = child->NextSiblingElement())
            walk(child, ctx);
    }

    void walk(const XMLElement* element, const Context& ctx)
    {
        const char* name = element->Name();
        if (std::strcmp(name, "g") == 0)
        {
            Context next = ctx;
            next.xform = element_transform(element, ctx);
            if (const char* id = element->Attribute("id"))
                next.element_id = id;
            if (has_class(element->Attribute("class"), "measure"))
            {
                MeasureBox measure;
                if (const char* id = element->Attribute("id"))
                    measure.id = id;
                next.measure = static_cast<int>(list_.measures.size());
                list_.measures.push_back(std::move(measure));
            }
            apply_class_styles(element->Attribute("class"), next);
            walk_children(element, next);
        }
        else if (std::strcmp(name, "use") == 0)
            handle_use(element, ctx);
        else if (std::strcmp(name, "path") == 0)
            handle_path(element, ctx);
        else if (std::strcmp(name, "rect") == 0)
            handle_rect(element, ctx);
        else if (std::strcmp(name, "polygon") == 0 || std::strcmp(name, "polyline") == 0)
            handle_poly(element, ctx, std::strcmp(name, "polygon") == 0);
        else if (std::strcmp(name, "ellipse") == 0 || std::strcmp(name, "circle") == 0)
            handle_ellipse(element, ctx);
        else if (std::strcmp(name, "line") == 0)
            handle_line(element, ctx);
        else if (std::strcmp(name, "text") == 0)
            handle_text(element, ctx);
        else if (std::strcmp(name, "style") == 0 || std::strcmp(name, "desc") == 0 || std::strcmp(name, "title") == 0 || std::strcmp(name, "metadata") == 0 || std::strcmp(name, "defs") == 0)
        {
            // Presentation/metadata containers handled elsewhere or irrelevant.
        }
        else
            warn(std::string("unhandled SVG element <") + name + ">");
    }

    static bool has_class(const char* class_attr, std::string_view wanted)
    {
        if (class_attr == nullptr)
            return false;
        std::string_view classes(class_attr);
        size_t pos = 0;
        while (pos < classes.size())
        {
            const size_t space = classes.find(' ', pos);
            const std::string_view cls = classes.substr(
                pos, space == std::string_view::npos ? classes.size() - pos : space - pos);
            if (cls == wanted)
                return true;
            if (space == std::string_view::npos)
                break;
            pos = space + 1;
        }
        return false;
    }

    // Every op emitted inside a measure group grows that measure's box.
    void extend_measure(const Context& ctx, glm::vec2 point)
    {
        if (ctx.measure < 0)
            return;
        MeasureBox& box = list_.measures[static_cast<size_t>(ctx.measure)];
        if (!box.valid)
        {
            box.min = box.max = point;
            box.valid = true;
            return;
        }
        box.min = glm::min(box.min, point);
        box.max = glm::max(box.max, point);
    }

    // Class-driven styles from Verovio's emitted stylesheet:
    //   g.ending, g.fing, g.reh, g.tempo   -> bold
    //   g.dir, g.dynam, g.mNum             -> italic
    static void apply_class_styles(const char* class_attr, Context& ctx)
    {
        if (class_attr == nullptr)
            return;
        std::string_view classes(class_attr);
        size_t pos = 0;
        while (pos < classes.size())
        {
            const size_t space = classes.find(' ', pos);
            const std::string_view cls = classes.substr(pos, space == std::string_view::npos ? classes.size() - pos : space - pos);
            if (cls == "ending" || cls == "fing" || cls == "reh" || cls == "tempo")
                ctx.bold = true;
            else if (cls == "dir" || cls == "dynam" || cls == "mNum")
                ctx.italic = true;
            if (space == std::string_view::npos)
                break;
            pos = space + 1;
        }
    }

    void handle_use(const XMLElement* element, const Context& ctx)
    {
        const char* href = element->Attribute("xlink:href");
        if (href == nullptr)
            href = element->Attribute("href");
        if (href == nullptr || href[0] != '#')
        {
            warn("<use> without local href");
            return;
        }
        const auto found = symbol_index_.find(href + 1);
        if (found == symbol_index_.end())
        {
            warn(std::string("<use> references unknown symbol ") + href);
            return;
        }
        GlyphInstance glyph;
        glyph.symbol_index = found->second;
        glyph.xform = element_transform(element, ctx);
        glyph.element_id = ctx.element_id;
        extend_measure(ctx, { glyph.xform.e, glyph.xform.f });
        list_.glyphs.push_back(std::move(glyph));
    }

    void push_path(std::vector<PathCmd>&& cmds, const XMLElement* element, const Context& ctx,
        bool force_fill, bool force_no_fill)
    {
        if (cmds.empty())
            return;
        DrawPath path;
        bool has_area = false;
        for (PathCmd& cmd : cmds)
        {
            cmd.p = ctx.xform.apply(cmd.p);
            cmd.c1 = ctx.xform.apply(cmd.c1);
            cmd.c2 = ctx.xform.apply(cmd.c2);
            if (cmd.op != PathCmd::Op::Close)
                extend_measure(ctx, cmd.p);
            if (cmd.op == PathCmd::Op::CubicTo || cmd.op == PathCmd::Op::Close)
                has_area = true;
        }
        path.cmds = std::move(cmds);

        const char* fill_attr = element->Attribute("fill");
        if (force_fill)
            path.fill = true;
        else if (force_no_fill || (fill_attr != nullptr && std::strcmp(fill_attr, "none") == 0))
            path.fill = false;
        else
        {
            // Verovio never writes fill="black"; per SVG everything defaults to
            // filled. Open M/L-only runs (staff lines, stems, barlines, tuplet
            // brackets) have no intended area though, so fill only shapes that
            // close or curve. This keeps angular bracket paths from becoming
            // solid triangles while filling slurs, ties, and dots correctly.
            path.fill = has_area;
        }

        if (path.fill)
            classify_hole_subpaths(path.cmds);

        const float stroke_width = element->FloatAttribute("stroke-width", 0.0f);
        path.stroke_width = stroke_width > 0.0f ? stroke_width * ctx.xform.uniform_scale() : 0.0f;
        if (!path.fill && path.stroke_width <= 0.0f)
            return; // nothing would render (e.g. bounding boxes)
        path.element_id = ctx.element_id;
        list_.paths.push_back(std::move(path));
    }

    void handle_path(const XMLElement* element, const Context& ctx)
    {
        const char* d = element->Attribute("d");
        std::vector<PathCmd> cmds;
        if (d == nullptr || !parse_svg_path_data(d, cmds))
        {
            warn("unparseable path data");
            return;
        }
        Context local = ctx;
        local.xform = element_transform(element, ctx);
        push_path(std::move(cmds), element, local, false, false);
    }

    void handle_rect(const XMLElement* element, const Context& ctx)
    {
        const float x = element->FloatAttribute("x", 0.0f);
        const float y = element->FloatAttribute("y", 0.0f);
        const float w = element->FloatAttribute("width", 0.0f);
        const float h = element->FloatAttribute("height", 0.0f);
        std::vector<PathCmd> cmds;
        cmds.push_back({ PathCmd::Op::MoveTo, { x, y } });
        cmds.push_back({ PathCmd::Op::LineTo, { x + w, y } });
        cmds.push_back({ PathCmd::Op::LineTo, { x + w, y + h } });
        cmds.push_back({ PathCmd::Op::LineTo, { x, y + h } });
        cmds.push_back({ PathCmd::Op::Close });
        push_path(std::move(cmds), element, ctx, true, false);
    }

    void handle_poly(const XMLElement* element, const Context& ctx, bool closed)
    {
        const char* points_attr = element->Attribute("points");
        if (points_attr == nullptr)
        {
            warn("<polygon>/<polyline> without points");
            return;
        }
        const std::vector<float> values = parse_number_list(points_attr);
        if (values.size() < 4)
            return;
        std::vector<PathCmd> cmds;
        cmds.push_back({ PathCmd::Op::MoveTo, { values[0], values[1] } });
        for (size_t i = 2; i + 1 < values.size(); i += 2)
            cmds.push_back({ PathCmd::Op::LineTo, { values[i], values[i + 1] } });
        if (closed)
            cmds.push_back({ PathCmd::Op::Close });
        push_path(std::move(cmds), element, ctx, closed, !closed);
    }

    void handle_ellipse(const XMLElement* element, const Context& ctx)
    {
        const float cx = element->FloatAttribute("cx", 0.0f);
        const float cy = element->FloatAttribute("cy", 0.0f);
        float rx = element->FloatAttribute("rx", 0.0f);
        float ry = element->FloatAttribute("ry", 0.0f);
        const float r = element->FloatAttribute("r", 0.0f);
        if (r > 0.0f)
        {
            rx = r;
            ry = r;
        }
        if (rx <= 0.0f || ry <= 0.0f)
            return;
        std::vector<PathCmd> cmds;
        append_ellipse(cmds, { cx, cy }, { rx, ry });
        push_path(std::move(cmds), element, ctx, true, false);
    }

    void handle_line(const XMLElement* element, const Context& ctx)
    {
        std::vector<PathCmd> cmds;
        cmds.push_back({ PathCmd::Op::MoveTo,
            { element->FloatAttribute("x1", 0.0f), element->FloatAttribute("y1", 0.0f) } });
        cmds.push_back({ PathCmd::Op::LineTo,
            { element->FloatAttribute("x2", 0.0f), element->FloatAttribute("y2", 0.0f) } });
        push_path(std::move(cmds), element, ctx, false, true);
    }

    static DrawText::Anchor parse_anchor(const char* anchor)
    {
        if (anchor != nullptr)
        {
            if (std::strcmp(anchor, "middle") == 0)
                return DrawText::Anchor::Middle;
            if (std::strcmp(anchor, "end") == 0)
                return DrawText::Anchor::End;
        }
        return DrawText::Anchor::Start;
    }

    struct TextRun
    {
        glm::vec2 pos{ 0.0f };
        float font_size = 0.0f;
        DrawText::Anchor anchor = DrawText::Anchor::Start;
        bool italic = false;
        bool bold = false;
        bool music_font = false;
        // Something was already emitted for this <text> element without an
        // intervening reposition; the next emitted run flows inline after it.
        bool continues = false;
        std::string content;
    };

    void flush_text_run(TextRun& run, const Context& ctx)
    {
        const std::string content = collapse_whitespace(run.content);
        run.content.clear();
        if (content.empty() || run.font_size <= 0.0f)
            return;
        DrawText text;
        text.pos = ctx.xform.apply(run.pos);
        text.font_size = run.font_size * ctx.xform.uniform_scale();
        text.anchor = run.anchor;
        text.italic = run.italic;
        text.bold = run.bold;
        text.music_font = run.music_font;
        text.continues_previous = run.continues;
        text.element_id = ctx.element_id;
        text.content = content;
        extend_measure(ctx, text.pos);
        list_.texts.push_back(std::move(text));
        run.continues = true;
    }

    // Text runs: tspans without their own x/y continue the current run;
    // explicit positions start a new one. Style attributes override inherited
    // class styles; a 0 outer font-size is Verovio's "size set on the tspans".
    void gather_text(const XMLElement* element, const Context& ctx, TextRun& run)
    {
        for (const XMLNode* node = element->FirstChild(); node != nullptr;
            node = node->NextSibling())
        {
            if (const XMLText* text_node = node->ToText())
            {
                run.content += text_node->Value();
                continue;
            }
            const XMLElement* child = node->ToElement();
            if (child == nullptr)
                continue;
            if (std::strcmp(child->Name(), "tspan") != 0)
            {
                warn(std::string("unhandled element <") + child->Name() + "> inside <text>");
                continue;
            }
            const TextRun saved = run;
            const bool repositioned = child->Attribute("x") != nullptr || child->Attribute("y") != nullptr;
            const bool restyled = child->Attribute("font-size") != nullptr || child->Attribute("font-style") != nullptr || child->Attribute("font-weight") != nullptr || child->Attribute("font-family") != nullptr || child->Attribute("text-anchor") != nullptr;

            // A tspan that changes position or style is its own run: emit any
            // pending content in the old style first, and emit this tspan's
            // content in its style before restoring for later siblings.
            if (repositioned || restyled)
                flush_text_run(run, ctx);
            if (repositioned)
            {
                run.pos = { child->FloatAttribute("x", saved.pos.x),
                    child->FloatAttribute("y", saved.pos.y) };
                run.continues = false; // explicit anchor breaks the inline flow
            }
            if (const char* size = child->Attribute("font-size"))
                run.font_size = std::strtof(size, nullptr);
            if (const char* style = child->Attribute("font-style"))
                run.italic = std::strcmp(style, "italic") == 0;
            if (const char* weight = child->Attribute("font-weight"))
                run.bold = std::strcmp(weight, "bold") == 0;
            if (const char* family = child->Attribute("font-family"))
                run.music_font = is_music_font(family);
            if (child->Attribute("text-anchor") != nullptr)
                run.anchor = parse_anchor(child->Attribute("text-anchor"));

            gather_text(child, ctx, run);

            if (repositioned || restyled)
                flush_text_run(run, ctx);
            run.pos = saved.pos;
            run.font_size = saved.font_size;
            run.italic = saved.italic;
            run.bold = saved.bold;
            run.music_font = saved.music_font;
            run.anchor = saved.anchor;
        }
    }

    void handle_text(const XMLElement* element, const Context& ctx)
    {
        TextRun run;
        run.pos = { element->FloatAttribute("x", 0.0f), element->FloatAttribute("y", 0.0f) };
        run.font_size = element->FloatAttribute("font-size", 0.0f);
        run.anchor = parse_anchor(element->Attribute("text-anchor"));
        run.italic = ctx.italic;
        run.bold = ctx.bold;
        gather_text(element, ctx, run);
        flush_text_run(run, ctx);
    }

    ScoreDrawList list_;
    std::unordered_map<std::string, int> symbol_index_;
    std::set<std::string> warnings_;
};

} // namespace

std::optional<ScoreDrawList> interpret_score_svg(std::string_view svg_text, std::string& error)
{
    Interpreter interpreter;
    return interpreter.run(svg_text, error);
}

} // namespace scoreview
} // namespace draxul

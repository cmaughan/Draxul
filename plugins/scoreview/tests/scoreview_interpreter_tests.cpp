#include <catch2/catch_all.hpp>

#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace draxul::scoreview;

namespace
{

std::string read_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

ScoreDrawList interpret_ok(const std::string& svg)
{
    std::string error;
    auto list = interpret_score_svg(svg, error);
    INFO(error);
    REQUIRE(list.has_value());
    return std::move(*list);
}

} // namespace

TEST_CASE("svg path data parser handles Verovio's command set", "[scoreview]")
{
    std::vector<PathCmd> cmds;

    SECTION("absolute move and lines")
    {
        REQUIRE(parse_svg_path_data("M1285 540 L4171 540", cmds));
        REQUIRE(cmds.size() == 2);
        CHECK(cmds[0].op == PathCmd::Op::MoveTo);
        CHECK(cmds[0].p == glm::vec2{ 1285.0f, 540.0f });
        CHECK(cmds[1].op == PathCmd::Op::LineTo);
        CHECK(cmds[1].p == glm::vec2{ 4171.0f, 540.0f });
    }

    SECTION("implicit repetition after move")
    {
        REQUIRE(parse_svg_path_data("M0 0 10 0 10 10", cmds));
        REQUIRE(cmds.size() == 3);
        CHECK(cmds[1].op == PathCmd::Op::LineTo);
        CHECK(cmds[2].p == glm::vec2{ 10.0f, 10.0f });
    }

    SECTION("relative cubics with repetition and close")
    {
        REQUIRE(parse_svg_path_data("M10 20c1 2 3 4 5 6 1 2 3 4 5 6z", cmds));
        REQUIRE(cmds.size() == 4);
        CHECK(cmds[1].op == PathCmd::Op::CubicTo);
        CHECK(cmds[1].c1 == glm::vec2{ 11.0f, 22.0f });
        CHECK(cmds[1].c2 == glm::vec2{ 13.0f, 24.0f });
        CHECK(cmds[1].p == glm::vec2{ 15.0f, 26.0f });
        CHECK(cmds[2].op == PathCmd::Op::CubicTo);
        CHECK(cmds[2].p == glm::vec2{ 20.0f, 32.0f });
        CHECK(cmds[3].op == PathCmd::Op::Close);
    }

    SECTION("smooth cubic reflects the previous control point")
    {
        REQUIRE(parse_svg_path_data("M0 0C10 0 20 10 30 10s20 10 30 0", cmds));
        REQUIRE(cmds.size() == 3);
        // Reflection of (20,10) about (30,10) is (40,10).
        CHECK(cmds[2].c1 == glm::vec2{ 40.0f, 10.0f });
        CHECK(cmds[2].c2 == glm::vec2{ 50.0f, 20.0f });
        CHECK(cmds[2].p == glm::vec2{ 60.0f, 10.0f });
    }

    SECTION("horizontal and vertical shorthands")
    {
        REQUIRE(parse_svg_path_data("M5 5h10v-3H0V9", cmds));
        REQUIRE(cmds.size() == 5);
        CHECK(cmds[1].p == glm::vec2{ 15.0f, 5.0f });
        CHECK(cmds[2].p == glm::vec2{ 15.0f, 2.0f });
        CHECK(cmds[3].p == glm::vec2{ 0.0f, 2.0f });
        CHECK(cmds[4].p == glm::vec2{ 0.0f, 9.0f });
    }

    SECTION("rejects unsupported commands and garbage")
    {
        CHECK_FALSE(parse_svg_path_data("M0 0 Q1 1 2 2", cmds));
        CHECK_FALSE(parse_svg_path_data("12 34", cmds));
        CHECK_FALSE(parse_svg_path_data("M0", cmds));
    }
}

TEST_CASE("interpreter converts the checked-in Verovio fixture exactly", "[scoreview]")
{
    // plugins/scoreview/tests/fixtures/svg/verovio-minimal-c4.svg is canned Verovio 6.2.1 output
    // for a single whole-note C4 measure. If a Verovio upgrade changes the SVG
    // dialect, this test is the tripwire — regenerate the fixture and revisit
    // the interpreter deliberately.
    const ScoreDrawList list = interpret_ok(
        read_file(std::string(DRAXUL_PROJECT_ROOT) + "/plugins/scoreview/tests/fixtures/svg/verovio-minimal-c4.svg"));

    for (const std::string& warning : list.warnings)
        UNSCOPED_INFO(warning);
    CHECK(list.warnings.empty());

    CHECK(list.pixel_size == glm::vec2{ 840.0f, 1188.0f });
    CHECK(list.canvas_size == glm::vec2{ 21000.0f, 29700.0f });

    // Symbols: G clef (E050), common-time digit 4 (E084), whole notehead (E0A2).
    REQUIRE(list.symbols.size() == 3);
    CHECK(list.symbols[0].id.substr(0, 4) == "E050");
    CHECK(list.symbols[1].id.substr(0, 4) == "E084");
    CHECK(list.symbols[2].id.substr(0, 4) == "E0A2");
    // The defs' scale(1,-1) is baked into the outline: "M198 133..." flips.
    REQUIRE_FALSE(list.symbols[2].cmds.empty());
    CHECK(list.symbols[2].cmds[0].op == PathCmd::Op::MoveTo);
    CHECK(list.symbols[2].cmds[0].p == glm::vec2{ 198.0f, -133.0f });

    // Winding classification: the whole-note head is an outer ring plus an
    // inner counter whose MoveTo must be flagged as a hole; the treble clef
    // carries several counters.
    {
        std::vector<size_t> moves;
        for (size_t i = 0; i < list.symbols[2].cmds.size(); ++i)
        {
            if (list.symbols[2].cmds[i].op == PathCmd::Op::MoveTo)
                moves.push_back(i);
        }
        REQUIRE(moves.size() == 2);
        CHECK_FALSE(list.symbols[2].cmds[moves[0]].hole);
        CHECK(list.symbols[2].cmds[moves[1]].hole);

        size_t clef_holes = 0;
        for (const PathCmd& cmd : list.symbols[0].cmds)
            clef_holes += (cmd.op == PathCmd::Op::MoveTo && cmd.hole) ? 1 : 0;
        CHECK(clef_holes >= 1);
    }

    // Glyphs: clef + two meter digits + notehead, page-margin translate applied.
    REQUIRE(list.glyphs.size() == 4);
    const GlyphInstance& notehead = list.glyphs[3];
    CHECK(notehead.symbol_index == 2);
    CHECK(notehead.xform.e == 3068.0f); // 500 page margin + 2568
    CHECK(notehead.xform.f == 1940.0f); // 500 page margin + 1440
    CHECK(notehead.xform.a == Catch::Approx(0.72));
    CHECK(notehead.xform.d == Catch::Approx(0.72));
    CHECK(notehead.element_id == "usfythd"); // the note's id, not the notehead g
    CHECK(list.glyphs[0].element_id == "u10l0y6g"); // clef id

    // Paths: 5 staff lines + 1 ledger line + 1 barline, all stroke-only.
    REQUIRE(list.paths.size() == 7);
    for (const DrawPath& path : list.paths)
    {
        CHECK_FALSE(path.fill);
        CHECK(path.stroke_width > 0.0f);
    }
    CHECK(list.paths[0].cmds[0].p == glm::vec2{ 1785.0f, 1040.0f });
    CHECK(list.paths[0].cmds[1].p == glm::vec2{ 4671.0f, 1040.0f });
    CHECK(list.paths[0].stroke_width == 13.0f);
    CHECK(list.paths[5].stroke_width == 22.0f); // ledger line
    CHECK(list.paths[6].stroke_width == 27.0f); // barline

    // Text: the part label, size taken from the inner tspan.
    REQUIRE(list.texts.size() == 1);
    CHECK(list.texts[0].content == "Piano");
    CHECK(list.texts[0].font_size == 405.0f);
    CHECK(list.texts[0].anchor == DrawText::Anchor::End);
    CHECK(list.texts[0].pos == glm::vec2{ 1605.0f, 1490.0f });
}

TEST_CASE("interpreter covers every construct in live Grieg output", "[scoreview]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);

    const std::string bytes = read_file(
        std::string(DRAXUL_PROJECT_ROOT) + "/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl");
    REQUIRE(engine->load(bytes, error));
    const int pages = engine->page_count();
    REQUIRE(pages >= 2);

    size_t total_glyphs = 0;
    size_t total_paths = 0;
    size_t total_texts = 0;
    for (int page = 1; page <= pages; ++page)
    {
        const ScoreDrawList list = interpret_ok(engine->render_page_svg(page));
        INFO("page " << page);
        for (const std::string& warning : list.warnings)
            UNSCOPED_INFO(warning);
        CHECK(list.warnings.empty()); // acceptance: zero unhandled constructs
        CHECK(list.canvas_size.x > 0.0f);
        CHECK(list.symbols.size() > 10);
        total_glyphs += list.glyphs.size();
        total_paths += list.paths.size();
        total_texts += list.texts.size();
    }
    CHECK(total_glyphs > 500);
    CHECK(total_paths > 800);
    CHECK(total_texts > 50);
}

TEST_CASE("interpreter marks opposite-winding subpaths as holes", "[scoreview]")
{
    // Outer square, one inner square wound opposite (a counter → hole), one
    // wound the same way (stays solid under the nonzero rule).
    const ScoreDrawList list = interpret_ok(R"(<svg viewBox="0 0 840 1188">
   <svg class="definition-scale" viewBox="0 0 21000 29700">
      <g id="donut">
         <path d="M0 0 L100 0 L100 100 L0 100 Z M20 20 L20 40 L40 40 L40 20 Z M60 60 L80 60 L80 80 L60 80 Z" stroke-width="1" />
      </g>
   </svg>
</svg>)");
    CHECK(list.warnings.empty());
    REQUIRE(list.paths.size() == 1);
    CHECK(list.paths[0].fill);
    std::vector<bool> holes;
    for (const PathCmd& cmd : list.paths[0].cmds)
    {
        if (cmd.op == PathCmd::Op::MoveTo)
            holes.push_back(cmd.hole);
    }
    REQUIRE(holes.size() == 3);
    CHECK_FALSE(holes[0]);
    CHECK(holes[1]);
    CHECK_FALSE(holes[2]);
}

TEST_CASE("interpreter flags music-font and continuation text runs", "[scoreview]")
{
    // Shape of a Verovio tempo mark: serif text, a metronome-note glyph in
    // the Leipzig music font, then more serif text — all one <text> anchor.
    const ScoreDrawList list = interpret_ok(R"(<svg viewBox="0 0 840 1188">
   <svg class="definition-scale" viewBox="0 0 21000 29700">
      <g id="t1" class="tempo">
         <text x="100" y="200" font-size="0px">
            <tspan font-size="405px">Allegro moderato </tspan>
            <tspan font-family="Leipzig" font-size="720px">Q</tspan>
            <tspan font-size="405px"> = 132</tspan>
         </text>
      </g>
   </svg>
</svg>)");
    CHECK(list.warnings.empty());
    REQUIRE(list.texts.size() == 3);
    CHECK(list.texts[0].content == "Allegro moderato");
    CHECK(list.texts[0].bold); // g.tempo class style
    CHECK_FALSE(list.texts[0].music_font);
    CHECK_FALSE(list.texts[0].continues_previous);
    CHECK(list.texts[1].content == "Q");
    CHECK(list.texts[1].music_font);
    CHECK(list.texts[1].continues_previous);
    CHECK(list.texts[1].font_size == 720.0f);
    CHECK(list.texts[2].content == "= 132");
    CHECK_FALSE(list.texts[2].music_font);
    CHECK(list.texts[2].continues_previous);
}

TEST_CASE("interpreter rejects structurally broken svg", "[scoreview]")
{
    std::string error;
    CHECK_FALSE(interpret_score_svg("<not-svg/>", error).has_value());
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(interpret_score_svg("<svg><g/></svg>", error).has_value());
    CHECK(error.find("definition-scale") != std::string::npos);
    error.clear();
    CHECK_FALSE(interpret_score_svg("garbage <<<", error).has_value());
}

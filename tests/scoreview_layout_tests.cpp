#include <catch2/catch_all.hpp>

#include <draxul/scoreview/verovio_layout_engine.h>
#include <draxul/scoreview/window_engraver.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

using namespace draxul::scoreview;

namespace
{

constexpr const char* MINIMAL_SCORE = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
      </attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>
    </measure>
  </part>
</score-partwise>)";

std::unique_ptr<VerovioLayoutEngine> make_engine()
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    return engine;
}

std::string read_binary_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("verovio engine reports missing resources", "[scoreview]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create("/nonexistent/verovio-data", error);
    CHECK(engine == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("verovio engine lays out a minimal score", "[scoreview]")
{
    auto engine = make_engine();
    CHECK_FALSE(engine->is_loaded());
    CHECK(engine->page_count() == 0);
    CHECK(engine->render_page_svg(1).empty());

    std::string error;
    REQUIRE(engine->load(MINIMAL_SCORE, error));
    CHECK(engine->is_loaded());
    CHECK(engine->page_count() == 1);

    const std::string svg = engine->render_page_svg(1);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("viewBox") != std::string::npos);
    CHECK(svg.find("<use") != std::string::npos); // SMuFL glyph instances
    CHECK(svg.find("staff") != std::string::npos); // staff group class

    CHECK(engine->render_page_svg(0).empty());
    CHECK(engine->render_page_svg(2).empty());

    error.clear();
    CHECK_FALSE(engine->load("", error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("verovio engine rejects garbage input", "[scoreview]")
{
    auto engine = make_engine();
    std::string error;
    CHECK_FALSE(engine->load("this is not any kind of score", error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(engine->is_loaded());
}

TEST_CASE("verovio engine lays out the Grieg .mxl and reflows", "[scoreview]")
{
    auto engine = make_engine();
    LayoutOptions options;
    options.page_size_px = { 840, 1188 };
    options.pixel_scale = 1.0f;
    engine->set_options(options);

    const std::string bytes = read_binary_file(
        std::string(DRAXUL_PROJECT_ROOT) + "/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl");
    std::string error;
    REQUIRE(engine->load(bytes, error));

    const int pages = engine->page_count();
    CHECK(pages >= 2); // 79 measures of piano music cannot fit one page
    for (int page = 1; page <= pages; ++page)
    {
        const std::string svg = engine->render_page_svg(page);
        INFO("page " << page);
        CHECK(svg.find("<svg") != std::string::npos);
        CHECK(svg.find("<use") != std::string::npos);
    }

    // Reflow to a much smaller page: layout survives and needs more pages.
    options.page_size_px = { 420, 594 };
    engine->set_options(options);
    CHECK(engine->page_count() > pages);
    CHECK(engine->render_page_svg(1).find("<svg") != std::string::npos);
}

TEST_CASE("window engraver engraves off-thread and echoes placement", "[scoreview]")
{
    std::string error;
    auto engraver = WindowEngraver::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engraver != nullptr);
    CHECK_FALSE(engraver->busy());

    WindowEngraver::Job job;
    job.window_xml = MINIMAL_SCORE;
    job.params.pixel_scale = 1.0f;
    job.params.marking_qpm = 120.0;
    job.first_bar = 3; // placement metadata is opaque to the engrave; echoed back
    job.count = 7;
    job.stream_offset_q = 12.0;
    engraver->submit(std::move(job));

    // Poll until the worker finishes (bounded so a hang fails rather than spins).
    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->ok);
    CHECK(done->first_bar == 3);
    CHECK(done->count == 7);
    CHECK(done->stream_offset_q == Catch::Approx(12.0));
    REQUIRE(done->window.strip != nullptr);
    CHECK(done->window.flow.ready()); // the timemap joined at least one onset
    CHECK(done->window.flow.mode() == FlowController::TransportMode::Roll);
    CHECK(engraver->poll() == std::nullopt); // result already taken
    CHECK_FALSE(engraver->busy()); // back to idle, ready to reuse

    // Reusing the worker and cancelling an in-flight engrave must not deadlock
    // or crash (cancel drains the worker and drops any pending result).
    WindowEngraver::Job again;
    again.window_xml = MINIMAL_SCORE;
    again.params.marking_qpm = 120.0;
    engraver->submit(std::move(again));
    engraver->cancel();
    CHECK_FALSE(engraver->busy());
}

TEST_CASE("window engraver reports a bad window without wedging", "[scoreview]")
{
    std::string error;
    auto engraver = WindowEngraver::create(DRAXUL_VEROVIO_DATA_DIR, error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job job;
    job.window_xml = "this is not a score";
    job.params.marking_qpm = 120.0;
    engraver->submit(std::move(job));

    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK_FALSE(done->ok); // garbage failed to engrave
    CHECK_FALSE(engraver->busy()); // and the worker is idle again
}

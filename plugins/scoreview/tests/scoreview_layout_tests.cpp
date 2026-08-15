#include <catch2/catch_all.hpp>

#include <draxul/scoreview/verovio_layout_engine.h>
#include <draxul/scoreview/window_engraver.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

struct BlockingEngineState
{
    std::mutex mutex;
    std::condition_variable changed;
    int load_calls = 0;
    int permits = 0;
    int destroyed = 0;
    std::vector<std::string> loaded;
};

class BlockingLayoutEngine final : public ILayoutEngine
{
public:
    explicit BlockingLayoutEngine(std::shared_ptr<BlockingEngineState> state)
        : state_(std::move(state))
    {
    }

    ~BlockingLayoutEngine() override
    {
        std::lock_guard lock(state_->mutex);
        ++state_->destroyed;
        state_->changed.notify_all();
    }

    bool load(std::string_view bytes, std::string& error) override
    {
        std::unique_lock lock(state_->mutex);
        ++state_->load_calls;
        const int call = state_->load_calls;
        state_->loaded.emplace_back(bytes);
        state_->changed.notify_all();
        state_->changed.wait(lock, [this, call]() { return state_->permits >= call; });
        error = "deterministic fake engraving failure";
        return false;
    }

    void set_options(const LayoutOptions&) override { }
    bool is_loaded() const override { return false; }
    int page_count() override { return 0; }
    std::string render_page_svg(int) override { return {}; }
    std::string render_timemap() override { return {}; }
    int midi_pitch_for_element(const std::string&) override { return -1; }
    int note_letter_for_element(const std::string&) override { return -1; }
    std::vector<std::string> tie_end_ids() override { return {}; }

private:
    std::shared_ptr<BlockingEngineState> state_;
};

bool wait_for_loads(const std::shared_ptr<BlockingEngineState>& state, int count)
{
    std::unique_lock lock(state->mutex);
    return state->changed.wait_for(
        lock, std::chrono::seconds(2), [&]() { return state->load_calls >= count; });
}

void release_loads(const std::shared_ptr<BlockingEngineState>& state, int count)
{
    std::lock_guard lock(state->mutex);
    state->permits = std::max(state->permits, count);
    state->changed.notify_all();
}

bool wait_until_idle(WindowEngraver& engraver)
{
    for (int i = 0; i < 400; ++i)
    {
        if (!engraver.busy())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return !engraver.busy();
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
        std::string(DRAXUL_PROJECT_ROOT) + "/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl");
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
    const WindowEngraver::RequestId request_id = engraver->submit(std::move(job));
    REQUIRE(request_id != 0);

    // Poll until the worker finishes (bounded so a hang fails rather than spins).
    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == request_id);
    CHECK(done->ok);
    CHECK(done->first_bar == 3);
    CHECK(done->count == 7);
    CHECK(done->stream_offset_q == Catch::Approx(12.0));
    REQUIRE(done->window.strip != nullptr);
    CHECK(done->window.flow.ready()); // the timemap joined at least one onset
    CHECK(done->window.flow.mode() == FlowController::TransportMode::Roll);
    CHECK(engraver->poll() == std::nullopt); // result already taken
    CHECK_FALSE(engraver->busy()); // back to idle, ready to reuse

    // Reusing the worker and invalidating an in-flight engrave must not
    // deadlock or expose the stale result.
    WindowEngraver::Job again;
    again.window_xml = MINIMAL_SCORE;
    again.params.marking_qpm = 120.0;
    REQUIRE(engraver->submit(std::move(again)) != 0);
    engraver->cancel();
    REQUIRE(wait_until_idle(*engraver));
    CHECK(engraver->poll() == std::nullopt);
}

TEST_CASE("window engraver reports a bad window without wedging", "[scoreview]")
{
    std::string error;
    auto engraver = WindowEngraver::create(DRAXUL_VEROVIO_DATA_DIR, error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job job;
    job.window_xml = "this is not a score";
    job.params.marking_qpm = 120.0;
    const WindowEngraver::RequestId request_id = engraver->submit(std::move(job));

    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == request_id);
    CHECK_FALSE(done->ok); // garbage failed to engrave
    CHECK_FALSE(engraver->busy()); // and the worker is idle again
}

TEST_CASE("window engraver coalesces rapid requests and publishes only the latest generation",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<BlockingEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(std::make_unique<BlockingLayoutEngine>(state), error);
    INFO(error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job first;
    first.window_xml = "first";
    first.first_bar = 1;
    const auto first_id = engraver->submit(std::move(first));
    REQUIRE(wait_for_loads(state, 1));

    WindowEngraver::Job superseded;
    superseded.window_xml = "superseded";
    superseded.first_bar = 2;
    const auto submit_start = std::chrono::steady_clock::now();
    const auto superseded_id = engraver->submit(std::move(superseded));
    WindowEngraver::Job latest;
    latest.window_xml = "latest";
    latest.first_bar = 3;
    const auto latest_id = engraver->submit(std::move(latest));
    const auto submit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - submit_start)
                               .count();
    CHECK(submit_ms < 50);
    CHECK(first_id < superseded_id);
    CHECK(superseded_id < latest_id);

    release_loads(state, 1);
    REQUIRE(wait_for_loads(state, 2));
    {
        std::lock_guard lock(state->mutex);
        REQUIRE(state->loaded.size() == 2);
        CHECK(state->loaded[0] == "first");
        CHECK(state->loaded[1] == "latest");
    }
    release_loads(state, 2);

    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done; ++i)
    {
        done = engraver->poll();
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == latest_id);
    CHECK(done->first_bar == 3);
    CHECK_FALSE(done->ok);
    CHECK_FALSE(engraver->busy());
}

TEST_CASE("window engraver cancellation is non-blocking generation invalidation",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<BlockingEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(std::make_unique<BlockingLayoutEngine>(state), error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job job;
    job.window_xml = "blocked";
    REQUIRE(engraver->submit(std::move(job)) != 0);
    REQUIRE(wait_for_loads(state, 1));

    const auto start = std::chrono::steady_clock::now();
    engraver->cancel();
    engraver->cancel();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                                .count();
    CHECK(elapsed_ms < 50);
    CHECK(engraver->busy()); // engine call may finish, but caller did not wait

    release_loads(state, 1);
    REQUIRE(wait_until_idle(*engraver));
    CHECK(engraver->poll() == std::nullopt);

    WindowEngraver::Job replacement;
    replacement.window_xml = "after-cancel";
    const auto replacement_id = engraver->submit(std::move(replacement));
    REQUIRE(replacement_id != 0);
    REQUIRE(wait_for_loads(state, 2));
    release_loads(state, 2);
    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done; ++i)
    {
        done = engraver->poll();
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == replacement_id);
}

TEST_CASE("window engraver rejects a missing injected engine", "[scoreview][engraver]")
{
    std::string error;
    auto engraver = WindowEngraver::create(std::unique_ptr<ILayoutEngine>{}, error);
    CHECK(engraver == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("window engraver shutdown owns and joins an active engine job",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<BlockingEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(std::make_unique<BlockingLayoutEngine>(state), error);
    REQUIRE(engraver != nullptr);
    WindowEngraver::Job job;
    job.window_xml = "shutdown";
    REQUIRE(engraver->submit(std::move(job)) != 0);
    REQUIRE(wait_for_loads(state, 1));

    std::atomic<bool> destroyed{ false };
    std::thread shutdown([owned = std::move(engraver), &destroyed]() mutable {
        owned.reset();
        destroyed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(destroyed.load()); // teardown joins rather than detaching Verovio
    release_loads(state, 1);
    shutdown.join();
    CHECK(destroyed.load());
    std::lock_guard lock(state->mutex);
    CHECK(state->destroyed == 1);
}

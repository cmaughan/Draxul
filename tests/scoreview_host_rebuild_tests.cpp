#include <catch2/catch_all.hpp>

#include <draxul/scoreview/score_host.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace draxul::scoreview
{

// This is the sole friend of ScoreHost used by tests. It drives the same
// restart/restyle methods as the UI while keeping test setup independent of a
// window, renderer, audio device, or real Verovio toolkit.
class ScoreHostTestAccess
{
public:
    static bool prime_window(
        ScoreHost& host, std::unique_ptr<ILayoutEngine> engine, std::string_view source,
        std::string& error)
    {
        host.engine_ = std::move(engine);
        if (!host.slicer_.load(std::string(source), error))
            return false;
        host.view_mode_ = ScoreHost::ViewMode::Flow;
        host.stream_windowed_ = true;
        host.engine_holds_window_ = false;
        host.initial_window_installed_ = false;
        host.piece_marking_qpm_ = 120.0;
        if (!host.rebuild_window(0, 0.0, false))
        {
            error = "initial synchronous window build failed";
            return false;
        }
        return true;
    }

    static void inject_engraver(ScoreHost& host, std::unique_ptr<WindowEngraver> engraver)
    {
        host.engraver_ = std::move(engraver);
    }

    static void set_transport(ScoreHost& host, double position_q, double tempo_qpm, bool playing)
    {
        host.flow_.seek(position_q);
        host.flow_.set_tempo_qpm(tempo_qpm);
        if (playing)
            host.flow_.play();
        else
            host.flow_.pause();
    }

    static void restart(ScoreHost& host)
    {
        host.restart_stream(true);
    }

    static void restyle_current_window(ScoreHost& host)
    {
        host.proportional_spacing_ = !host.proportional_spacing_;
        host.reengrave_flow_in_place();
    }

    static void poll(ScoreHost& host)
    {
        host.poll_async_engrave();
    }

    static void deliver_stale_completion(ScoreHost& host, WindowEngraver::RequestId request_id)
    {
        WindowEngraver::Done done;
        done.request_id = request_id;
        done.ok = true;
        host.handle_async_engrave_done(std::move(done));
    }

    static std::shared_ptr<const ScoreDrawList> strip(const ScoreHost& host)
    {
        return host.strip_;
    }

    static WindowEngraver::RequestId pending_request(const ScoreHost& host)
    {
        return host.pending_window_install_ ? host.pending_window_install_->request_id : 0;
    }

    static bool async_pending(const ScoreHost& host)
    {
        return host.async_engrave_in_flight_;
    }

    static double position_q(const ScoreHost& host)
    {
        return host.flow_.position_q();
    }

    static double tempo_qpm(const ScoreHost& host)
    {
        return host.flow_.tempo_qpm();
    }

    static bool playing(const ScoreHost& host)
    {
        return host.flow_.playing();
    }
};

} // namespace draxul::scoreview

namespace
{

using draxul::scoreview::ILayoutEngine;
using draxul::scoreview::LayoutOptions;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::WindowEngraver;

constexpr std::string_view kMinimalScore = R"xml(<?xml version="1.0" encoding="UTF-8"?>
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
</score-partwise>)xml";

constexpr std::string_view kTimemap = R"json([
  {"qstamp": 0, "tempo": 120, "on": ["usfythd"]},
  {"qstamp": 4, "off": ["usfythd"]}
])json";

std::string read_svg_fixture()
{
    const auto path = std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "tests/fixtures/svg/verovio-minimal-c4.svg";
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

struct FakeEngineState
{
    std::mutex mutex;
    std::condition_variable changed;
    int load_calls = 0;
    int permits = 0;
    std::vector<std::string> payloads;
};

class DeterministicLayoutEngine final : public ILayoutEngine
{
public:
    DeterministicLayoutEngine(
        std::shared_ptr<FakeEngineState> state, std::string svg, bool block_load)
        : state_(std::move(state))
        , svg_(std::move(svg))
        , block_load_(block_load)
    {
    }

    bool load(std::string_view bytes, std::string& error) override
    {
        std::unique_lock lock(state_->mutex);
        ++state_->load_calls;
        const int call = state_->load_calls;
        state_->payloads.emplace_back(bytes);
        state_->changed.notify_all();
        if (block_load_)
            state_->changed.wait(lock, [&]() { return state_->permits >= call; });
        loaded_ = true;
        error.clear();
        return true;
    }

    void set_options(const LayoutOptions&) override { }
    bool is_loaded() const override { return loaded_; }
    int page_count() override { return loaded_ ? 1 : 0; }
    std::string render_page_svg(int page_number) override
    {
        return loaded_ && page_number == 1 ? svg_ : std::string{};
    }
    std::string render_timemap() override
    {
        return loaded_ ? std::string(kTimemap) : std::string{};
    }
    int midi_pitch_for_element(const std::string& element_id) override
    {
        return element_id == "usfythd" ? 60 : -1;
    }
    int note_letter_for_element(const std::string& element_id) override
    {
        return element_id == "usfythd" ? 0 : -1;
    }
    std::vector<std::string> tie_end_ids() override { return {}; }

private:
    std::shared_ptr<FakeEngineState> state_;
    std::string svg_;
    bool block_load_ = false;
    bool loaded_ = false;
};

bool wait_for_loads(const std::shared_ptr<FakeEngineState>& state, int count)
{
    std::unique_lock lock(state->mutex);
    return state->changed.wait_for(
        lock, std::chrono::seconds(2), [&]() { return state->load_calls >= count; });
}

void release_loads(const std::shared_ptr<FakeEngineState>& state, int count)
{
    std::lock_guard lock(state->mutex);
    state->permits = std::max(state->permits, count);
    state->changed.notify_all();
}

bool wait_for_host_install(ScoreHost& host)
{
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        ScoreHostTestAccess::poll(host);
        if (!ScoreHostTestAccess::async_pending(host))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

TEST_CASE("ScoreHost restart and restyle remain nonblocking behind an active engrave",
    "[scoreview][host][engraver][lifetime]")
{
    const std::string svg = read_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);

    auto worker_state = std::make_shared<FakeEngineState>();
    auto worker_engine =
        std::make_unique<DeterministicLayoutEngine>(worker_state, svg, true);
    auto engraver = WindowEngraver::create(std::move(worker_engine), error);
    INFO(error);
    REQUIRE(engraver);
    ScoreHostTestAccess::inject_engraver(host, std::move(engraver));

    ScoreHostTestAccess::set_transport(host, 1.0, 96.0, true);
    const auto old_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(old_strip);
    const double old_position = ScoreHostTestAccess::position_q(host);
    const double old_tempo = ScoreHostTestAccess::tempo_qpm(host);

    ScoreHostTestAccess::restyle_current_window(host);
    const auto first_request = ScoreHostTestAccess::pending_request(host);
    REQUIRE(first_request != 0);
    REQUIRE(wait_for_loads(worker_state, 1));

    const auto restart_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restart(host);
    const auto restart_elapsed = std::chrono::steady_clock::now() - restart_start;
    const auto restart_request = ScoreHostTestAccess::pending_request(host);

    const auto restyle_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restyle_current_window(host);
    const auto restyle_elapsed = std::chrono::steady_clock::now() - restyle_start;
    const auto latest_request = ScoreHostTestAccess::pending_request(host);

    CHECK(restart_elapsed < std::chrono::milliseconds(50));
    CHECK(restyle_elapsed < std::chrono::milliseconds(50));
    CHECK(first_request < restart_request);
    CHECK(restart_request < latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    // Exercise the host's generation check directly: a delayed completion
    // from the first request cannot clear or replace the latest pending job.
    ScoreHostTestAccess::deliver_stale_completion(host, first_request);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());

    // Completing the first generation must not expose it to ScoreHost: the
    // newest restyle starts, while the old engraving and transport stay live.
    release_loads(worker_state, 1);
    REQUIRE(wait_for_loads(worker_state, 2));
    ScoreHostTestAccess::poll(host);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    {
        std::lock_guard lock(worker_state->mutex);
        CHECK(worker_state->load_calls == 2);
        CHECK(worker_state->payloads.size() == 2);
    }

    release_loads(worker_state, 2);
    REQUIRE(wait_for_host_install(host));
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

TEST_CASE("ScoreHost uses synchronous restart and restyle when its worker is unavailable",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);
    REQUIRE(main_state->load_calls == 1);

    ScoreHostTestAccess::set_transport(host, 1.0, 102.0, true);
    const double tempo = ScoreHostTestAccess::tempo_qpm(host);
    const auto initial_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restart(host);

    CHECK(main_state->load_calls == 2);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != initial_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(0.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    ScoreHostTestAccess::set_transport(host, 1.0, tempo, true);
    const auto restarted_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restyle_current_window(host);
    CHECK(main_state->load_calls == 3);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::strip(host).get() != restarted_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(1.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

// ScoreHost orchestration coverage (kanban 15): the real host's lifecycle —
// initialize/pump/status/viewport/shutdown — driven headlessly through the
// shared fixture. No GPU, window, microphone, MIDI keyboard, audio output,
// or user dialog is required by any test here; device layers are reached
// only through their fakes or not at all.

#include <catch2/catch_all.hpp>

#include "support/scoreview_host_fixture.h"
#include "support/temp_dir.h"

#include "score_session_controller.h"

#include <draxul/scoreview/progress_store.h>

#include <draxul/host.h>

#include <fstream>
#include <memory>
#include <string>

namespace
{

using draxul::HostContext;
using draxul::HostViewport;
using draxul::scoreview::CountingHostCallbacks;
using draxul::scoreview::DeterministicLayoutEngine;
using draxul::scoreview::FakeEngineState;
using draxul::scoreview::kScoreHostFixtureMinimalScore;
using draxul::scoreview::NoteOutcome;
using draxul::scoreview::NoteVerdict;
using draxul::scoreview::PlayerInputRig;
using draxul::scoreview::read_verovio_svg_fixture;
using draxul::scoreview::release_loads;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::ScoreSessionController;
using draxul::scoreview::wait_for_loads;
using draxul::scoreview::WindowEngraver;

HostViewport viewport(int w, int h)
{
    HostViewport view;
    view.pixel_size = { w, h };
    view.pixel_scale = 1.0f;
    return view;
}

// A primed rolling-window host (fake engine, no devices), shared by the
// input-selection and shutdown tests.
struct PrimedHost
{
    std::shared_ptr<FakeEngineState> engine_state = std::make_shared<FakeEngineState>();
    ScoreHost host;

    bool prime()
    {
        const std::string svg = read_verovio_svg_fixture();
        if (svg.empty())
            return false;
        std::string error;
        return ScoreHostTestAccess::prime_window(host,
            std::make_unique<DeterministicLayoutEngine>(engine_state, svg, false),
            kScoreHostFixtureMinimalScore, error);
    }
};

} // namespace

TEST_CASE("a sourceless host runs one full headless lifecycle",
    "[scoreview][host][orchestration]")
{
    auto callbacks = std::make_unique<CountingHostCallbacks>();
    auto host = std::make_unique<ScoreHost>();

    HostContext context;
    context.initial_viewport = viewport(800, 600);
    REQUIRE(host->initialize(context, *callbacks));
    CHECK(host->is_running());
    CHECK(host->init_error().empty());

    // The placeholder page: pump, interrogate, resize — no GPU touched.
    for (int frame = 0; frame < 3; ++frame)
        host->pump();
    CHECK_FALSE(host->status_text().empty());
    (void)host->runtime_state();
    (void)host->debug_state();
    (void)host->print_hint();
    (void)host->default_background();
    host->set_viewport(viewport(1024, 768));
    host->pump();

    host->shutdown();
    CHECK_FALSE(host->is_running());
    host->shutdown(); // idempotent
    CHECK_FALSE(host->is_running());

    // Callbacks must not be reached after shutdown: destroy the host while
    // the callbacks object is still alive, then release the callbacks.
    const int frames_at_shutdown = callbacks->frames;
    host.reset();
    CHECK(callbacks->frames == frames_at_shutdown);
}

TEST_CASE("a missing source fails initialize cleanly and shutdown stays safe",
    "[scoreview][host][orchestration]")
{
    CountingHostCallbacks callbacks;
    ScoreHost host;

    HostContext context;
    context.initial_viewport = viewport(640, 480);
    context.launch_options.source_path = "/nonexistent/draxul-test-piece.musicxml";
    REQUIRE_FALSE(host.initialize(context, callbacks));
    CHECK_FALSE(host.is_running());
    CHECK(host.init_error().find("could not open score file") != std::string::npos);

    // Shutdown from the partially-initialized state, twice.
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("shutdown is safe from a never-initialized host", "[scoreview][host][orchestration]")
{
    ScoreHost host;
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("shutdown retires a completed but uninstalled window generation",
    "[scoreview][host][orchestration]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());

    // A blockable worker completes an engrave the host never polls.
    auto worker_state = std::make_shared<FakeEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(
        std::make_unique<DeterministicLayoutEngine>(
            worker_state, read_verovio_svg_fixture(), true),
        error);
    REQUIRE(engraver);
    ScoreHostTestAccess::inject_engraver(primed.host, std::move(engraver));

    ScoreHostTestAccess::restyle_current_window(primed.host);
    REQUIRE(ScoreHostTestAccess::pending_request(primed.host) != 0);
    release_loads(worker_state, 1);
    REQUIRE(wait_for_loads(worker_state, 1));

    // No poll: the finished generation is still queued when the host goes
    // down. Shutdown must join the worker and drop the pending install
    // without hanging or installing into a dead presentation.
    primed.host.shutdown();
    CHECK_FALSE(primed.host.is_running());
}

namespace
{

// An engine whose load() always fails: the layout-failure path.
class FailingLayoutEngine final : public draxul::scoreview::ILayoutEngine
{
public:
    bool load(std::string_view, std::string& error) override
    {
        error = "deterministic layout failure";
        return false;
    }
    void set_options(const draxul::scoreview::LayoutOptions&) override {}
    bool is_loaded() const override
    {
        return false;
    }
    int page_count() override
    {
        return 0;
    }
    std::string render_page_svg(int) override
    {
        return {};
    }
    std::string render_timemap() override
    {
        return {};
    }
    int midi_pitch_for_element(const std::string&) override
    {
        return -1;
    }
    int note_letter_for_element(const std::string&) override
    {
        return -1;
    }
    std::vector<std::string> tie_end_ids() override
    {
        return {};
    }
};

} // namespace

TEST_CASE("a layout failure degrades to the monolithic fallback, shutdown stays safe",
    "[scoreview][host][orchestration]")
{
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(
        host, std::make_unique<FailingLayoutEngine>(), kScoreHostFixtureMinimalScore, error));
    CHECK_FALSE(error.empty());
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("input selection swaps in place and reports requested-vs-engaged",
    "[scoreview][host][orchestration][input]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());
    ScoreHostTestAccess::set_transport(primed.host, 1.5, 96.0, true);
    const double position = ScoreHostTestAccess::position_q(primed.host);
    const double tempo = ScoreHostTestAccess::tempo_qpm(primed.host);

    using GateInput = ScoreHostTestAccess::GateInput;
    CHECK(ScoreHostTestAccess::select_input(primed.host, GateInput::Keyboard));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Keyboard);

    // A MIDI request with no device engages the keyboard instead and says so.
    CHECK_FALSE(ScoreHostTestAccess::select_input(
        primed.host, GateInput::Midi, /*midi_port=*/-1));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Keyboard);

    // The bot always engages (deterministic verification player).
    CHECK(ScoreHostTestAccess::select_input(primed.host, GateInput::Bot));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Bot);

    // Swaps never touch the session: transport survives every switch.
    CHECK(ScoreHostTestAccess::position_q(primed.host) == Catch::Approx(position));
    CHECK(ScoreHostTestAccess::tempo_qpm(primed.host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(primed.host));
}

TEST_CASE("the session controller survives a corrupt progress file",
    "[scoreview][host][orchestration][session]")
{
    const draxul::tests::TempDir progress_dir("scoreview-progress");
    const std::string source_bytes = "fake-source-bytes-for-hashing";

    // A first session writes real progress.
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        REQUIRE(session.attached());
        session.model().set_piece("Test Piece", 120.0, 4.0);
        REQUIRE(session.begin_session());
        NoteOutcome outcome;
        outcome.onset_q = 0.0;
        outcome.pitch = 60;
        outcome.verdict = NoteVerdict::Correct;
        outcome.quality = 1.0;
        session.model().apply(outcome);
        session.mark_dirty();
        session.end_session(0.75); // final flush
    }

    // A second controller loads it back.
    std::filesystem::path stored_file;
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().total_notes_judged() == 1);
        CHECK(session.model().sessions().size() == 1);
        stored_file = draxul::scoreview::progress_path(progress_dir.path, source_bytes);
        REQUIRE(std::filesystem::exists(stored_file));
    }

    // Corrupt the file: the third controller starts fresh instead of crashing
    // or half-loading, and can still begin a session and save.
    {
        std::ofstream corrupt(stored_file, std::ios::binary | std::ios::trunc);
        corrupt << "{ this is not json";
    }
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().total_notes_judged() == 0);
        CHECK(session.model().sessions().empty());
        REQUIRE(session.begin_session());
        session.mark_dirty();
        session.flush_at_bar(1);
    }
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().sessions().size() == 1); // the fresh record took over
    }
}

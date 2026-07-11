#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/notation/musicxml_importer.h>
#include <draxul/runtime_path.h>
#include <draxul/scoreview/bot_player_input.h>
#include <draxul/scoreview/score_render_nvg.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include "nanovg.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace draxul
{
namespace scoreview
{

namespace
{

constexpr float SQRT2 = 1.41421356f;
constexpr float MIN_ZOOM = 0.4f;
constexpr float MAX_ZOOM = 4.0f;
// Verification bot (G4): slow enough that adaptation must pull the start
// tempo (60% of marking) downward measurably within a short capture window.
constexpr double kBotPaceQpm = 50.0;

// Placeholder engraving proportions (no-source mode), see plans/scoreview.md
// phase 0. Staff-relative thicknesses follow SMuFL engravingDefaults.
constexpr float STAFF_HEIGHT_FRAC = 0.045f;
constexpr float STAFF_LINE_THICKNESS_SP = 0.13f;
constexpr float BARLINE_THIN_SP = 0.16f;
constexpr float BARLINE_THICK_SP = 0.5f;
constexpr float BARLINE_SEPARATION_SP = 0.4f;
constexpr int PLACEHOLDER_MEASURES = 4;

const NVGcolor INK = { { { 0.12f, 0.11f, 0.10f, 1.0f } } };
// Reading-room backdrop behind the pages; drawn inside the pane only. The
// host's default_background stays the app-standard terminal color so window
// clear, pane borders, and grid remainder strips match every other pane.
const NVGcolor BACKDROP = { { { 0.22f, 0.23f, 0.24f, 1.0f } } };

void fill_backdrop(NVGcontext* vg, int w, int h)
{
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    nvgFillColor(vg, BACKDROP);
    nvgFill(vg);
}

void fill_vertical_bar(NVGcontext* vg, float x_center, float top, float bottom, float width)
{
    nvgBeginPath(vg);
    nvgRect(vg, x_center - width * 0.5f, top, width, bottom - top);
    nvgFillColor(vg, INK);
    nvgFill(vg);
}

// Curly brace joining the two staves, built from four cubics pinched at the
// tips and waist. Placeholder only — real pages use the Bravura brace glyph.
void fill_brace(NVGcontext* vg, float right_x, float top, float bottom, float sp)
{
    const float mid = (top + bottom) * 0.5f;
    const float half = mid - top;
    const float lobe = right_x - 2.4f * sp;
    const float inner = right_x - 1.5f * sp;
    const float waist_x = right_x - 0.5f * sp;

    nvgBeginPath(vg);
    nvgMoveTo(vg, right_x, top);
    nvgBezierTo(vg, lobe, top + half * 0.30f, lobe, mid - half * 0.12f, waist_x, mid);
    nvgBezierTo(vg, lobe, mid + half * 0.12f, lobe, bottom - half * 0.30f, right_x, bottom);
    nvgBezierTo(vg, inner, bottom - half * 0.30f, inner, mid + half * 0.12f, waist_x, mid);
    nvgBezierTo(vg, inner, mid - half * 0.12f, inner, top + half * 0.30f, right_x, top);
    nvgClosePath(vg);
    nvgFillColor(vg, INK);
    nvgFill(vg);
}

void draw_placeholder(NVGcontext* vg, int width, int height, float pixel_scale)
{
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    fill_backdrop(vg, width, height);

    const float outer_margin = 24.0f * pixel_scale;
    const float avail_w = fw - 2.0f * outer_margin;
    const float avail_h = fh - 2.0f * outer_margin;
    if (avail_w < 64.0f || avail_h < 64.0f)
        return;

    float page_h = avail_h;
    float page_w = page_h / SQRT2;
    if (page_w > avail_w)
    {
        page_w = avail_w;
        page_h = page_w * SQRT2;
    }
    const float px = (fw - page_w) * 0.5f;
    const float py = (fh - page_h) * 0.5f;
    draw_page_sheet(vg, px, py, page_w, page_h, pixel_scale);

    const float staff_h = page_h * STAFF_HEIGHT_FRAC;
    const float sp = staff_h / 4.0f;
    const float line_w = std::max(1.0f, STAFF_LINE_THICKNESS_SP * sp);
    const float left = px + page_w * 0.10f;
    const float right = px + page_w * 0.90f;
    const float upper_top = py + page_h * 0.18f;
    const float lower_top = upper_top + staff_h + 1.5f * staff_h;
    const float system_bottom = lower_top + staff_h;

    for (int staff = 0; staff < 2; ++staff)
    {
        const float top = staff == 0 ? upper_top : lower_top;
        for (int i = 0; i < 5; ++i)
        {
            const float y = top + static_cast<float>(i) * sp;
            nvgBeginPath(vg);
            nvgMoveTo(vg, left, y);
            nvgLineTo(vg, right, y);
            nvgStrokeColor(vg, INK);
            nvgStrokeWidth(vg, line_w);
            nvgStroke(vg);
        }
    }

    const float thin_w = std::max(1.0f, BARLINE_THIN_SP * sp);
    const float thick_w = BARLINE_THICK_SP * sp;
    for (int i = 0; i < PLACEHOLDER_MEASURES; ++i)
    {
        const float x = left + (right - left) * static_cast<float>(i) / PLACEHOLDER_MEASURES;
        fill_vertical_bar(vg, x, upper_top, system_bottom, thin_w);
    }
    const float final_thin_x = right - thick_w - BARLINE_SEPARATION_SP * sp - thin_w * 0.5f;
    fill_vertical_bar(vg, final_thin_x, upper_top, system_bottom, thin_w);
    fill_vertical_bar(vg, right - thick_w * 0.5f, upper_top, system_bottom, thick_w);

    fill_brace(vg, left - 0.6f * sp, upper_top, system_bottom, sp);
}

} // namespace

bool ScoreHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    viewport_ = context.initial_viewport;
    callbacks_ = &callbacks;
    source_path_ = context.launch_options.source_path;
    background_ = context.launch_options.terminal_bg.value_or(background_);

    nanovg_pass_ = create_nanovg_pass();
    if (!nanovg_pass_)
    {
        init_error_ = "failed to create NanoVG render pass";
        return false;
    }

    if (!source_path_.empty())
    {
        std::ifstream stream(source_path_, std::ios::binary);
        if (!stream)
        {
            init_error_ = "could not open score file '" + source_path_ + "'";
            return false;
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        const std::string bytes = buffer.str();

        // Semantic model import is best-effort here: it powers status metadata
        // now and editing later. Compressed .mxl bytes are engine-only until
        // the model importer grows zip support.
        std::string import_error;
        if (auto imported = notation::import_musicxml(bytes, import_error))
        {
            for (const std::string& warning : imported->warnings)
                DRAXUL_LOG_DEBUG(LogCategory::App, "score model import: %s", warning.c_str());
            model_ = std::move(imported->document);
            has_model_ = true;
        }
        else
        {
            DRAXUL_LOG_DEBUG(
                LogCategory::App, "score model import skipped: %s", import_error.c_str());
        }

        const std::string resources = (executable_directory() / "verovio-data").string();
        std::string engine_error;
        auto engine = VerovioLayoutEngine::create(resources, engine_error);
        if (!engine)
        {
            init_error_ = engine_error;
            return false;
        }
        if (!engine->load(bytes, engine_error))
        {
            init_error_ = engine_error + " ('" + source_path_ + "')";
            return false;
        }
        engine_ = std::move(engine);
        layout_dirty_ = true;

        // Dev/test hooks: `--command flow` starts in the conveyor view,
        // `flow-autoplay` also starts the transport; `--command gate` starts
        // in gate mode with the keyboard input, `gate-mic` with the acoustic
        // listener, `gate-bot` runs the scripted verification bot
        // (`gate-bot-err` with 70% accuracy).
        const std::string& command = context.launch_options.command;
        if (command.find("gate") != std::string::npos)
        {
            view_mode_ = ViewMode::Flow;
            flow_dirty_ = true;
            start_in_gate_ = true;
            gate_input_requested_ = GateInput::Keyboard;
            if (command.find("bot") != std::string::npos)
                gate_input_requested_ = GateInput::Bot;
            else if (command.find("mic") != std::string::npos)
                gate_input_requested_ = GateInput::Mic;
            gate_bot_accuracy_ = command.find("err") != std::string::npos ? 0.7 : 1.0;
        }
        else if (command.find("flow") != std::string::npos)
        {
            view_mode_ = ViewMode::Flow;
            flow_dirty_ = true;
            flow_autoplay_ = command.find("autoplay") != std::string::npos;
        }
    }

    epoch_ = std::chrono::steady_clock::now();
    last_pump_ = epoch_;
    running_ = true;
    return true;
}

double ScoreHost::now_seconds() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch_).count();
}

void ScoreHost::shutdown()
{
    strip_.reset();
    pages_.reset();
    engine_.reset();
    nanovg_pass_.reset();
    running_ = false;
}

bool ScoreHost::is_running() const
{
    return running_;
}

void ScoreHost::set_viewport(const HostViewport& viewport)
{
    const bool size_changed = viewport.pixel_size != viewport_.pixel_size || viewport.pixel_scale != viewport_.pixel_scale;
    viewport_ = viewport;
    if (size_changed)
        layout_dirty_ = true;
}

void ScoreHost::on_config_reloaded(const HostReloadConfig& config)
{
    if (config.terminal_bg)
        background_ = *config.terminal_bg;
}

float ScoreHost::ui_scale() const
{
    return viewport_.pixel_scale > 0.0f ? viewport_.pixel_scale : 1.0f;
}

ScoreHost::FlowBand ScoreHost::flow_band() const
{
    const float vh = static_cast<float>(viewport_.pixel_size.y);
    FlowBand band;
    band.target_h = std::clamp(vh * 0.35f * zoom_, 96.0f * ui_scale(), vh * 0.9f);
    band.strip_y = (vh - band.target_h) * 0.5f;
    band.band_pad = 18.0f * ui_scale();
    return band;
}

float ScoreHost::page_margin() const
{
    return 24.0f * ui_scale();
}

float ScoreHost::page_gap() const
{
    return 24.0f * ui_scale();
}

float ScoreHost::content_height() const
{
    if (!pages_ || pages_->empty())
        return 0.0f;
    const float count = static_cast<float>(pages_->size());
    return 2.0f * page_margin() + count * page_height_px_ + (count - 1.0f) * page_gap();
}

float ScoreHost::max_scroll() const
{
    return std::max(0.0f, content_height() - static_cast<float>(viewport_.pixel_size.y));
}

void ScoreHost::scroll_by(float delta_px)
{
    scroll_to(scroll_y_ + delta_px);
}

void ScoreHost::scroll_to(float scroll_px)
{
    const float clamped = std::clamp(scroll_px, 0.0f, max_scroll());
    if (clamped == scroll_y_)
        return;
    scroll_y_ = clamped;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::set_zoom(float zoom)
{
    const float clamped = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    if (clamped == zoom_)
        return;
    zoom_ = clamped;
    // Paged zoom re-engraves at the new scale; the flow strip is
    // resolution-independent and just draws at a different height.
    if (view_mode_ == ViewMode::Paged)
        layout_dirty_ = true;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

int ScoreHost::current_page() const
{
    if (!pages_ || pages_->empty() || page_height_px_ <= 0.0f)
        return 1;
    const float focus = scroll_y_ + static_cast<float>(viewport_.pixel_size.y) * 0.5f - page_margin();
    const int page = static_cast<int>(std::floor(focus / (page_height_px_ + page_gap())));
    return std::clamp(page, 0, static_cast<int>(pages_->size()) - 1) + 1;
}

void ScoreHost::relayout()
{
    if (!engine_ || !engine_->is_loaded() || viewport_.pixel_size.x <= 0 || viewport_.pixel_size.y <= 0)
        return;

    const float margin = page_margin();
    const int avail_w = std::max(64, static_cast<int>(static_cast<float>(viewport_.pixel_size.x) - 2.0f * margin));

    LayoutOptions options;
    options.page_size_px = { avail_w, static_cast<int>(std::lround(avail_w * SQRT2)) };
    options.pixel_scale = ui_scale() * zoom_;
    engine_->set_options(options);

    auto pages = std::make_shared<std::vector<ScoreDrawList>>();
    const int count = engine_->page_count();
    for (int page = 1; page <= count; ++page)
    {
        std::string error;
        auto list = interpret_score_svg(engine_->render_page_svg(page), error);
        if (!list)
        {
            DRAXUL_LOG_DEBUG(LogCategory::App, "score page %d interpret failed: %s", page,
                error.c_str());
            continue;
        }
        for (const std::string& warning : list->warnings)
            DRAXUL_LOG_DEBUG(
                LogCategory::App, "score page %d interpreter: %s", page, warning.c_str());
        pages->push_back(std::move(*list));
    }

    pages_ = pages;
    size_t total_ops = 0;
    for (const ScoreDrawList& page : *pages_)
        total_ops += page.glyphs.size() + page.paths.size() + page.texts.size();
    DRAXUL_LOG_INFO(LogCategory::App, "score: engraved %zu page(s), %zu draw ops (%dpx wide, zoom %.0f%%)",
        pages_->size(), total_ops, avail_w, static_cast<double>(zoom_) * 100.0);
    if (!pages_->empty() && pages_->front().canvas_size.x > 0.0f)
    {
        const ScoreDrawList& first = pages_->front();
        page_width_px_ = static_cast<float>(avail_w);
        page_scale_ = page_width_px_ / first.canvas_size.x;
        page_height_px_ = first.canvas_size.y * page_scale_;
    }
    layout_dirty_ = false;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::relayout_flow()
{
    flow_dirty_ = false;
    if (!engine_ || !engine_->is_loaded())
        return;

    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    options.pixel_scale = ui_scale();
    engine_->set_options(options);

    std::string error;
    auto strip = interpret_score_svg(engine_->render_page_svg(1), error);
    if (!strip)
    {
        DRAXUL_LOG_ERROR(
            LogCategory::App, "score: flow interpret failed, staying paged: %s", error.c_str());
        view_mode_ = ViewMode::Paged;
        layout_dirty_ = true;
        return;
    }
    for (const std::string& warning : strip->warnings)
        DRAXUL_LOG_DEBUG(LogCategory::App, "score flow interpreter: %s", warning.c_str());

    auto shared_strip = std::make_shared<const ScoreDrawList>(std::move(*strip));
    auto timemap = parse_timemap(engine_->render_timemap(), error);
    const bool transport_ok = timemap.has_value() && flow_.build(*timemap, *shared_strip, error);
    if (!transport_ok)
        DRAXUL_LOG_ERROR(
            LogCategory::App, "score: conveyor transport unavailable: %s", error.c_str());

    strip_ = std::move(shared_strip);
    highlight_.build(*strip_);
    apply_lit_update(); // anything at q <= 0 sits under the playhead pre-lit
    DRAXUL_LOG_INFO(LogCategory::App, "score: conveyor strip %zu ops, %zu onsets, marking %d qpm",
        strip_->glyphs.size() + strip_->paths.size() + strip_->texts.size(),
        flow_.onsets().size(), static_cast<int>(std::lround(flow_.marking_qpm())));

    if (transport_ok)
    {
        // Expected notes + tie continuations for the gate (same id space —
        // no model bridge needed, plans/scoreview-gate.md).
        flow_.prepare_gates(
            [this](const std::string& id) { return engine_->midi_pitch_for_element(id); },
            engine_->tie_end_ids());
        if (start_in_gate_)
        {
            start_in_gate_ = false;
            enter_gate_mode(gate_input_requested_, kBotPaceQpm, gate_bot_accuracy_);
        }
    }

    if (flow_autoplay_ && transport_ok)
    {
        flow_.play();
        flow_autoplay_ = false;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::toggle_flow_mode()
{
    if (!engine_ || !engine_->is_loaded())
        return;
    if (view_mode_ == ViewMode::Paged)
    {
        view_mode_ = ViewMode::Flow;
        flow_dirty_ = true; // re-engrave as the strip on the next pump
    }
    else
    {
        if (flow_.mode() == FlowController::TransportMode::Gate)
            exit_gate_mode();
        view_mode_ = ViewMode::Paged;
        flow_.pause();
        layout_dirty_ = true; // re-engrave pages; scroll_y_ carries over
    }
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::apply_lit_update()
{
    const FlowController::LitUpdate update = flow_.take_lit_update();
    if (update.reset)
        highlight_.clear_lit();
    for (const std::string& id : update.newly_lit)
        highlight_.set_lit(id);
}

void ScoreHost::apply_verdict_update()
{
    const FlowController::VerdictUpdate update = flow_.take_verdict_update();
    if (update.reset)
        highlight_.clear_lit();
    for (const auto& [id, verdict] : update.changes)
    {
        highlight_.set_state(id,
            verdict == FlowController::NoteVerdict::Missed ? ScoreHighlightState::State::Missed
                                                           : ScoreHighlightState::State::Correct);
    }
}

bool ScoreHost::set_gate_input(GateInput input, double bot_pace_qpm, double bot_accuracy)
{
    keyboard_input_ = nullptr;
    mic_input_ = nullptr;
    player_input_.reset();
    if (input == GateInput::Bot)
    {
        player_input_ = std::make_unique<BotPlayerInput>(flow_, bot_pace_qpm, bot_accuracy, 20260711u);
        return true;
    }
    if (input == GateInput::Mic)
    {
        // Opening is asynchronous (the TCC consent dialog can block for
        // minutes); Opening counts as engaged, and pump() falls back to the
        // keyboard if the open ultimately fails.
        auto mic = std::make_unique<MicPlayerInput>(flow_);
        if (mic->state() != MicPlayerInput::State::Failed)
        {
            mic_input_ = mic.get();
            player_input_ = std::move(mic);
            return true;
        }
        DRAXUL_LOG_WARN(LogCategory::App, "score: %s — falling back to keyboard input",
            mic->error().c_str());
        // fall through to the keyboard
    }
    auto keyboard = std::make_unique<KeyboardPlayerInput>();
    keyboard_input_ = keyboard.get();
    player_input_ = std::move(keyboard);
    return input == GateInput::Keyboard;
}

void ScoreHost::enter_gate_mode(GateInput input, double bot_pace_qpm, double bot_accuracy)
{
    if (!flow_.gates_ready())
        return;
    flow_.set_mode(FlowController::TransportMode::Gate);
    highlight_.clear_lit();
    apply_verdict_update(); // consume the reset
    const bool engaged = set_gate_input(input, bot_pace_qpm, bot_accuracy);
    // Bots start themselves, and the piano IS the mic session's interface —
    // no Space press needed. The keyboard player starts with Space.
    if (input != GateInput::Keyboard && engaged)
        flow_.play();
    last_logged_gate_ = 0;
    logged_gate_end_ = false;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::exit_gate_mode()
{
    player_input_.reset();
    keyboard_input_ = nullptr;
    mic_input_ = nullptr;
    flow_.set_mode(FlowController::TransportMode::Clock);
    highlight_.clear_lit();
    apply_lit_update(); // consume the reset; re-light anything at q <= 0
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

bool ScoreHost::handle_gate_key(int keycode)
{
    if (flow_.mode() != FlowController::TransportMode::Gate)
        return false;
    if (keycode == SDLK_ESCAPE)
    {
        exit_gate_mode();
        return true;
    }
    // `i` switches the human input source mid-session (mic <-> keyboard)
    // without touching verdicts, score, or the transport.
    if (keycode == SDLK_I && (keyboard_input_ != nullptr || mic_input_ != nullptr))
    {
        const GateInput next = mic_input_ != nullptr ? GateInput::Keyboard : GateInput::Mic;
        const bool engaged = set_gate_input(next, 0.0, 1.0);
        DRAXUL_LOG_INFO(LogCategory::App, "score: gate input -> %s",
            mic_input_ != nullptr ? "microphone" : "keyboard");
        if (next == GateInput::Mic && engaged && !flow_.playing())
            flow_.play(); // the piano is the interface; don't demand Space
        return true;
    }
    if (keyboard_input_ == nullptr)
        return false; // bot/mic session: the piano row doesn't inject notes

    // Dev piano row (scaffolding only): z s x d c v g b h n j m , = one
    // chromatic octave anchored at the armed gate's register; Return plays
    // the gate correctly (oracle); Backspace plays a guaranteed-wrong note.
    const std::vector<int> expected = flow_.armed_required_pitches();
    const int reference = expected.empty() ? 60 : expected.front();
    const int anchor = reference - (reference % 12);
    int semitone = -1;
    switch (keycode)
    {
    case SDLK_Z:
        semitone = 0;
        break;
    case SDLK_S:
        semitone = 1;
        break;
    case SDLK_X:
        semitone = 2;
        break;
    case SDLK_D:
        semitone = 3;
        break;
    case SDLK_C:
        semitone = 4;
        break;
    case SDLK_V:
        semitone = 5;
        break;
    case SDLK_G:
        semitone = 6;
        break;
    case SDLK_B:
        semitone = 7;
        break;
    case SDLK_H:
        semitone = 8;
        break;
    case SDLK_N:
        semitone = 9;
        break;
    case SDLK_J:
        semitone = 10;
        break;
    case SDLK_M:
        semitone = 11;
        break;
    case SDLK_COMMA:
        semitone = 12;
        break;
    default:
        break;
    }
    if (semitone >= 0)
    {
        keyboard_input_->push(anchor + semitone, now_seconds());
        return true;
    }
    if (keycode == SDLK_RETURN)
    {
        for (const int pitch : expected)
            keyboard_input_->push(pitch, now_seconds());
        return true;
    }
    if (keycode == SDLK_BACKSPACE)
    {
        int wrong = anchor + 1;
        while (std::find(expected.begin(), expected.end(), wrong) != expected.end())
            ++wrong;
        keyboard_input_->push(wrong, now_seconds());
        return true;
    }
    return false;
}

int ScoreHost::approx_measure() const
{
    if (!flow_.ready() || !has_model_)
        return 0;
    double quarters_per_measure = 0.0;
    if (!model_.parts.empty())
    {
        for (const auto& measure : model_.parts[0].measures)
        {
            if (measure.time)
            {
                quarters_per_measure = measure.time->measure_duration().to_double() * 4.0;
                break;
            }
        }
    }
    if (quarters_per_measure <= 0.0)
        return 0;
    return static_cast<int>(flow_.position_q() / quarters_per_measure) + 1;
}

void ScoreHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    // Clamp long stalls (first frame, app pauses) to one frame's worth.
    const double dt = std::clamp(std::chrono::duration<double>(now - last_pump_).count(), 0.0, 0.1);
    last_pump_ = now;

    if (view_mode_ == ViewMode::Flow && flow_dirty_)
        relayout_flow();
    if (view_mode_ == ViewMode::Paged && layout_dirty_)
        relayout();

    if (view_mode_ == ViewMode::Flow && flow_.playing())
    {
        flow_.advance(dt);
        if (flow_.mode() == FlowController::TransportMode::Gate)
        {
            if (mic_input_ != nullptr && mic_input_->state() == MicPlayerInput::State::Failed)
            {
                DRAXUL_LOG_WARN(LogCategory::App, "score: %s — falling back to keyboard input",
                    mic_input_->error().c_str());
                set_gate_input(GateInput::Keyboard, 0.0, 1.0);
            }
            if (player_input_ != nullptr)
            {
                std::vector<PlayerNoteEvent> events;
                player_input_->poll(now_seconds(), events);
                if (!events.empty())
                    flow_.judge(events);
            }
            apply_verdict_update();

            // Periodic + final INFO lines feed the G4 log-based verification.
            const size_t gate_index = flow_.armed_gate_index();
            if (gate_index / 8 != last_logged_gate_ / 8)
            {
                DRAXUL_LOG_INFO(LogCategory::App,
                    "score: gate progress — gate %zu, tempo %d qpm, score %d, streak %d, misses %d",
                    gate_index, static_cast<int>(std::lround(flow_.tempo_qpm())), flow_.score(),
                    flow_.streak(), flow_.miss_count());
            }
            last_logged_gate_ = gate_index;
            if (flow_.at_end() && !logged_gate_end_)
            {
                logged_gate_end_ = true;
                DRAXUL_LOG_INFO(LogCategory::App,
                    "score: gate finished — tempo %d qpm, score %d, misses %d",
                    static_cast<int>(std::lround(flow_.tempo_qpm())), flow_.score(),
                    flow_.miss_count());
            }
        }
        else
        {
            apply_lit_update();
        }
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
    }
}

std::optional<std::chrono::steady_clock::time_point> ScoreHost::next_deadline() const
{
    if (view_mode_ == ViewMode::Flow && flow_.playing())
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    return std::nullopt;
}

void ScoreHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    const float pixel_scale = ui_scale();
    if (view_mode_ == ViewMode::Flow && strip_ && strip_->canvas_size.y > 0.0f)
    {
        // The conveyor: the whole piece as one strip on a full-width band,
        // scrolled so the playhead anchor tracks the transport position.
        auto strip = strip_;
        const ScoreHighlightState* highlight = &highlight_;
        const float vw = static_cast<float>(viewport_.pixel_size.x);
        const float vh = static_cast<float>(viewport_.pixel_size.y);
        const FlowBand band = flow_band();
        const float target_h = band.target_h;
        const float scale = target_h / strip->canvas_size.y;
        constexpr double kAnchorFrac = 0.3;
        const double scroll_canvas = flow_.scroll_x(static_cast<double>(vw) / scale, kAnchorFrac);
        const float origin_x = static_cast<float>(-scroll_canvas * scale);
        const float strip_y = band.strip_y;
        const float playhead_x = static_cast<float>((flow_.x_at(flow_.position_q()) - scroll_canvas) * scale);
        const bool waiting = flow_.waiting();

        nanovg_pass_->set_draw_callback(
            [strip, highlight, pixel_scale, vw, target_h, scale, origin_x, strip_y, playhead_x,
                waiting](NVGcontext* vg, int w, int h) {
                fill_backdrop(vg, w, h);
                const float band_pad = 18.0f * pixel_scale;
                draw_page_sheet(vg, -48.0f * pixel_scale, strip_y - band_pad,
                    vw + 96.0f * pixel_scale, target_h + 2.0f * band_pad, pixel_scale);
                const ScoreTextFonts fonts = ensure_score_text_fonts(vg);
                render_draw_list(vg, *strip, { origin_x, strip_y }, scale, fonts, highlight);
                // Playhead: amber while rolling, teal while awaiting the player.
                nvgBeginPath(vg);
                nvgRect(vg, playhead_x - 1.0f * pixel_scale, strip_y - band_pad,
                    2.0f * pixel_scale, target_h + 2.0f * band_pad);
                nvgFillColor(vg,
                    waiting ? nvgRGBA(24, 140, 165, 200) : nvgRGBA(217, 115, 20, 170));
                nvgFill(vg);
            });
    }
    else if (!pages_ || pages_->empty())
    {
        nanovg_pass_->set_draw_callback([pixel_scale](NVGcontext* vg, int w, int h) {
            draw_placeholder(vg, w, h, pixel_scale);
        });
    }
    else
    {
        auto pages = pages_;
        const float margin = page_margin();
        const float gap = page_gap();
        const float scroll = scroll_y_;
        const float page_w = page_width_px_;
        const float page_h = page_height_px_;
        const float scale = page_scale_;
        nanovg_pass_->set_draw_callback(
            [pages, pixel_scale, margin, gap, scroll, page_w, page_h, scale](
                NVGcontext* vg, int w, int h) {
                fill_backdrop(vg, w, h);
                const ScoreTextFonts fonts = ensure_score_text_fonts(vg);
                float y = margin - scroll;
                for (const ScoreDrawList& page : *pages)
                {
                    if (y + page_h >= 0.0f && y <= static_cast<float>(h))
                    {
                        draw_page_sheet(vg, margin, y, page_w, page_h, pixel_scale);
                        render_draw_list(vg, page, { margin, y }, scale, fonts);
                    }
                    y += page_h + gap;
                }
            });
    }

    RenderViewport vp;
    vp.x = viewport_.pixel_pos.x;
    vp.y = viewport_.pixel_pos.y;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
    frame.flush_submit_chunk();
}

void ScoreHost::on_key(const KeyEvent& event)
{
    if (!event.pressed)
        return;
    if (event.keycode == SDLK_F)
    {
        toggle_flow_mode();
        return;
    }
    if (view_mode_ == ViewMode::Flow)
    {
        if (handle_gate_key(event.keycode))
        {
            if (callbacks_ != nullptr)
                callbacks_->request_frame();
            return;
        }
        if (event.keycode == SDLK_G && flow_.mode() == FlowController::TransportMode::Clock && flow_.gates_ready())
        {
            enter_gate_mode(GateInput::Keyboard, 0.0, 1.0);
            return;
        }
        switch (event.keycode)
        {
        case SDLK_SPACE:
            if (flow_.playing())
                flow_.pause();
            else
                flow_.play();
            break;
        case SDLK_LEFTBRACKET:
            flow_.set_tempo_qpm(flow_.tempo_qpm() / 1.04);
            break;
        case SDLK_RIGHTBRACKET:
            flow_.set_tempo_qpm(flow_.tempo_qpm() * 1.04);
            break;
        case SDLK_R:
            flow_.rewind();
            apply_lit_update();
            break;
        default:
            return;
        }
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
        return;
    }
    const float line_step = 60.0f * ui_scale();
    const float page_step = static_cast<float>(viewport_.pixel_size.y) * 0.9f;
    switch (event.keycode)
    {
    case SDLK_DOWN:
    case SDLK_J:
        scroll_by(line_step);
        break;
    case SDLK_UP:
    case SDLK_K:
        scroll_by(-line_step);
        break;
    case SDLK_PAGEDOWN:
    case SDLK_SPACE:
        scroll_by(page_step);
        break;
    case SDLK_PAGEUP:
        scroll_by(-page_step);
        break;
    case SDLK_HOME:
        scroll_to(0.0f);
        break;
    case SDLK_END:
        scroll_to(max_scroll());
        break;
    default:
        break;
    }
}

void ScoreHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    scroll_by(-event.delta.y * 40.0f * ui_scale());
}

bool ScoreHost::dispatch_action(std::string_view action)
{
    if (action == "font_increase")
    {
        set_zoom(zoom_ * 1.15f);
        return true;
    }
    if (action == "font_decrease")
    {
        set_zoom(zoom_ / 1.15f);
        return true;
    }
    if (action == "font_reset")
    {
        set_zoom(1.0f);
        return true;
    }
    return false;
}

void ScoreHost::request_close()
{
    running_ = false;
}

std::string ScoreHost::status_text() const
{
    if (source_path_.empty())
        return "score: no --source (placeholder)";

    std::string title;
    if (has_model_ && !model_.title.empty())
    {
        title = model_.title;
        if (!model_.composer.empty())
            title += " — " + model_.composer;
    }
    else
    {
        title = std::filesystem::path(source_path_).filename().string();
    }

    std::string status = "score: " + title;
    if (view_mode_ == ViewMode::Flow && flow_.ready())
    {
        const int qpm = static_cast<int>(std::lround(flow_.tempo_qpm()));
        const int pct = static_cast<int>(std::lround(flow_.tempo_qpm() / flow_.marking_qpm() * 100.0));
        if (flow_.mode() == FlowController::TransportMode::Gate)
        {
            status += flow_.waiting()
                ? "  WAIT"
                : (flow_.at_end() ? "  end" : (flow_.playing() ? "  >" : "  ||"));
            if (mic_input_ != nullptr)
            {
                if (mic_input_->state() == MicPlayerInput::State::Ready)
                {
                    // Input level 0-9: the at-a-glance "it hears me" meter.
                    const int level = std::clamp(
                        static_cast<int>(mic_input_->level() * 9.99f), 0, 9);
                    status += "  MIC" + std::to_string(level);
                }
                else
                {
                    status += "  MIC?"; // waiting on device / permission
                }
            }
            status += "  " + std::to_string(qpm) + "qpm (" + std::to_string(pct) + "%)";
            status += "  score " + std::to_string(flow_.score());
            status += "  x" + std::to_string(flow_.streak());
            if (flow_.miss_count() > 0)
                status += "  miss " + std::to_string(flow_.miss_count());
            return status;
        }
        status += flow_.playing() ? "  >" : (flow_.at_end() ? "  end" : "  ||");
        status += "  " + std::to_string(qpm) + "qpm (" + std::to_string(pct) + "%)";
        const int measure = approx_measure();
        if (measure > 0)
            status += "  m." + std::to_string(measure);
        return status;
    }
    if (pages_ && !pages_->empty())
    {
        status += "  p. " + std::to_string(current_page()) + "/" + std::to_string(pages_->size());
    }
    if (zoom_ != 1.0f)
        status += "  " + std::to_string(static_cast<int>(std::lround(zoom_ * 100.0f))) + "%";
    return status;
}

Color ScoreHost::default_background() const
{
    // The primary host's background becomes the window clear color, which
    // shows through pane borders and grid remainder strips — it must match
    // the terminal scheme exactly or every pane's chrome shifts hue. The
    // score's own mid-gray backdrop is drawn inside the pane instead.
    return background_;
}

HostRuntimeState ScoreHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = running_ && (!engine_ || pages_ != nullptr);
    return state;
}

HostDebugState ScoreHost::debug_state() const
{
    HostDebugState state;
    state.name = "ScoreView";
    return state;
}

HostPrintHint ScoreHost::print_hint() const
{
    // Print the music, not the pane furniture: crop away the mid-gray
    // backdrop around the page/band, and snap the screen-tuned warm sheet
    // tint to paper white (it prints as visible stipple otherwise).
    HostPrintHint hint;
    hint.paper_white = true;
    const float vw = static_cast<float>(viewport_.pixel_size.x);
    const float vh = static_cast<float>(viewport_.pixel_size.y);
    if (view_mode_ == ViewMode::Flow && strip_ && strip_->canvas_size.y > 0.0f)
    {
        const FlowBand band = flow_band();
        const float y0 = std::clamp(band.strip_y - band.band_pad, 0.0f, vh);
        const float y1 = std::clamp(band.strip_y + band.target_h + band.band_pad, y0, vh);
        hint.content_pos = { 0, static_cast<int>(y0) };
        hint.content_size = { static_cast<int>(vw), static_cast<int>(y1 - y0) };
    }
    else if (pages_ && !pages_->empty() && page_width_px_ > 0.0f)
    {
        // Inset past the sheet's rounded corners (backdrop shows through
        // them); the page's own engraving margins keep music clear of it.
        const float inset = 4.0f * ui_scale();
        const float margin = page_margin();
        const float y0 = std::clamp(margin - scroll_y_ + inset, 0.0f, vh);
        const float y1 = std::clamp(content_height() - margin - scroll_y_ - inset, y0, vh);
        hint.content_pos = { static_cast<int>(margin + inset), static_cast<int>(y0) };
        hint.content_size = { static_cast<int>(page_width_px_ - 2.0f * inset),
            static_cast<int>(y1 - y0) };
    }
    return hint;
}

std::unique_ptr<IHost> create_score_host()
{
    return std::make_unique<ScoreHost>();
}

void register_score_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Score, &create_score_host);
}

} // namespace scoreview
} // namespace draxul

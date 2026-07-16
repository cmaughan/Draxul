#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/notation/musicxml_importer.h>
#include <draxul/runtime_path.h>
#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/progress_store.h>
#include <draxul/scoreview/score_render_nvg.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>
#include <draxul/sdl_imgui_input.h>

#include <ctime>

#include "nanovg.h"
#include "score_audio_controller.h"
#include "score_session_controller.h"

#include <SDL3/SDL.h>
#include <imgui.h>

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
// Rolling window (stream S2): bars around the playhead that fill the pane
// width — the user's "show just 2 bars"; zoom divides it.
constexpr double kStreamVisibleBars = 2.0;

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

// Out of line so the unique_ptr members over internal component types
// (ScoreAudioController) destroy where the complete type is visible.
ScoreHost::ScoreHost()
    : audio_(std::make_unique<ScoreAudioController>())
    , session_(std::make_unique<ScoreSessionController>())
{
}

ScoreHost::~ScoreHost() = default;

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
        source_bytes_ = bytes; // the rolling window re-slices the source

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

        // The background window engraver gets its OWN Verovio toolkit so it can
        // engrave the next rolling window off the main thread (async
        // double-buffer). If it cannot start, the stream falls back to
        // synchronous window rebuilds.
        std::string engraver_error;
        engraver_ = WindowEngraver::create(resources, engraver_error);
        if (!engraver_)
            DRAXUL_LOG_WARN(LogCategory::App,
                "score: background engraver unavailable (%s); window swaps stay synchronous",
                engraver_error.c_str());

        // Instrument voices: every .sf2 staged beside the app is offered in
        // the inspector (the bundled YDP grand by default; a better font
        // dropped into the folder just appears). Loading is lazy — the
        // ~118 MB parse happens on first selection, not startup.
        audio_->stage_soundfonts(executable_directory() / "soundfonts");

        // Player memory: the per-piece progress file, keyed by the source
        // bytes so renames don't lose history (stream plan S0).
        session_->attach_source(
            ConfigDocument::default_path().parent_path() / "scoreview" / "progress", bytes);

        // Default: the runner (plans/scoreview-runner.md) — the conveyor in
        // Roll mode with the dev keyboard, transport rolling from the first
        // note (mic via `roll-mic` or `i`). Commands override: `paged` =
        // the reading view, `flow`/`flow-autoplay` = clock conveyor,
        // `gate*` = the wait-mode dev instrument (incl. the verification
        // bots), `roll-mic` = the runner listening to the piano.
        view_mode_ = ViewMode::Flow;
        flow_dirty_ = true;
        start_in_gate_ = true;
        gate_input_requested_ = GateInput::Keyboard;
        game_mode_ = FlowController::TransportMode::Roll;
        const std::string& command = context.launch_options.command;
        if (command.find("paged") != std::string::npos)
        {
            view_mode_ = ViewMode::Paged;
            flow_dirty_ = false;
            start_in_gate_ = false;
        }
        else if (command.find("gate") != std::string::npos)
        {
            game_mode_ = FlowController::TransportMode::Gate;
            if (command.find("bot") != std::string::npos)
                gate_input_requested_ = GateInput::Bot;
            else if (command.find("mic") != std::string::npos)
                gate_input_requested_ = GateInput::Mic;
            gate_bot_accuracy_ = command.find("err") != std::string::npos ? 0.7 : 1.0;
        }
        else if (command.find("roll") != std::string::npos)
        {
            if (command.find("mic") != std::string::npos)
                gate_input_requested_ = GateInput::Mic;
        }
        else if (command.find("flow") != std::string::npos)
        {
            start_in_gate_ = false;
            flow_autoplay_ = command.find("autoplay") != std::string::npos;
        }
        // `mono` disables the rolling window (the monolithic-strip
        // verification instrument for window-equivalence checks).
        if (command.find("mono") != std::string::npos)
            stream_windowed_ = false;
        // The metronome defaults ON with subdivisions; `notick`/`tick`/
        // `tick8` tokens override from launch (dev/test).
        if (command.find("notick") != std::string::npos)
            audio_->set_tick_level(ScoreAudioController::TickLevel::Off);
        if (command.find("notes") != std::string::npos)
            audio_->set_audition(true);
        if (command.find("nowaterfall") != std::string::npos)
            show_waterfall_ = false;
        if (command.find("noverdict") != std::string::npos
            || command.find("nomistake") != std::string::npos)
            mark_mistakes_ = false;
        if (command.find("fullcolor") != std::string::npos)
            split_accidentals_ = false;
        // The composer defaults OFF while its program is tuned; `composer`
        // opts in at launch, `nocomposer` still forces it off (checked first
        // — "composer" is a substring of "nocomposer").
        if (command.find("nocomposer") != std::string::npos)
            composer_enabled_ = false;
        else if (command.find("composer") != std::string::npos)
            composer_enabled_ = true;
        if (command.find("locktempo") != std::string::npos)
            lock_tempo_ = true;
        else if (command.find("tick8") != std::string::npos)
            audio_->set_tick_level(ScoreAudioController::TickLevel::Eighths);
        else if (command.find("tick") != std::string::npos)
            audio_->set_tick_level(ScoreAudioController::TickLevel::Beats);
    }

    // The debug/learning inspector gets its own ImGui context (the pattern
    // the 3D hosts use). attach_imgui_host wires the backend once it exists.
    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    if (imgui_context_ != nullptr)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
    }
    last_imgui_time_ = std::chrono::steady_clock::now();

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
    end_progress_session();
    // Stop the background engraver first: its destructor signals the worker and
    // joins it (bounded by one in-flight engrave), so nothing races teardown.
    engraver_.reset();
    audio_->shutdown();
    strip_.reset();
    pages_.reset();
    engine_.reset();
    nanovg_pass_.reset();
    if (imgui_backend_ != nullptr)
    {
        if (imgui_context_ != nullptr)
            ImGui::SetCurrentContext(imgui_context_);
        imgui_backend_->shutdown_imgui_backend();
        imgui_backend_ = nullptr;
    }
    if (imgui_context_ != nullptr)
    {
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
    }
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

    // The paged (reading) view is always the WHOLE piece: if the engine is
    // currently holding a rolling-window slice (the Roll runner), reload the
    // full source first — otherwise pagination would show only the window.
    if (engine_holds_window_)
    {
        std::string reload_error;
        if (engine_->load(source_bytes_, reload_error))
        {
            engine_holds_window_ = false;
            stream_offset_q_ = 0.0;
        }
        else
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "score: source reload for paged view failed: %s", reload_error.c_str());
        }
    }

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
    if (engine_holds_window_)
    {
        // Leaving the windowed roll: the clock conveyor and paged view work
        // on the whole piece again.
        std::string reload_error;
        if (!engine_->load(source_bytes_, reload_error))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "score: source reload failed: %s",
                reload_error.c_str());
            return;
        }
        engine_holds_window_ = false;
        stream_offset_q_ = 0.0;
    }

    std::string error;
    const FlowBuildResult result = build_flow_from_engine(error);
    // A fresh engraving: re-fit the fixed score band's scale to the new
    // content (the window high-water-mark restarts from the first window).
    stream_scale_ref_ = 0.0f;
    stream_scale_ = 0.0f;
    if (result == FlowBuildResult::InterpretFailed)
    {
        DRAXUL_LOG_ERROR(
            LogCategory::App, "score: flow interpret failed, staying paged: %s", error.c_str());
        view_mode_ = ViewMode::Paged;
        layout_dirty_ = true;
        return;
    }
    const bool transport_ok = result == FlowBuildResult::Ok;
    if (!transport_ok)
        DRAXUL_LOG_ERROR(
            LogCategory::App, "score: conveyor transport unavailable: %s", error.c_str());

    const double bar_quarters = quarters_per_measure_from_model();
    quarters_per_bar_ = bar_quarters > 0.0 ? bar_quarters : 4.0;
    piece_marking_qpm_ = flow_.marking_qpm();
    session_->model().set_piece(has_model_ && !model_.title.empty()
            ? model_.title
            : std::filesystem::path(source_path_).filename().string(),
        flow_.marking_qpm(), quarters_per_bar_);
    apply_lit_update(); // anything at q <= 0 sits under the playhead pre-lit
    DRAXUL_LOG_INFO(LogCategory::App, "score: conveyor strip %zu ops, %zu onsets, marking %d qpm",
        strip_->glyphs.size() + strip_->paths.size() + strip_->texts.size(),
        flow_.onsets().size(), static_cast<int>(std::lround(flow_.marking_qpm())));

    if (transport_ok)
    {
        // The rolling window needs the sliceable source; .mxl (zip) sources
        // fall back to the monolithic strip (recorded follow-up).
        if (stream_windowed_ && !slicer_.ready())
        {
            std::string slicer_error;
            if (!slicer_.load(source_bytes_, slicer_error) || slicer_.bar_count() < 2)
            {
                stream_windowed_ = false;
                DRAXUL_LOG_WARN(LogCategory::App,
                    "score: rolling window unavailable, monolithic strip (%s)",
                    slicer_error.c_str());
            }
        }
        // Source compatibility is the composer's call (IComposer::supports):
        // the adaptive stream needs a single-part source for its grand-staff
        // drill bars; unsupported pieces stream the source verbatim.
        composing_ = composer_enabled_ && stream_windowed_ && composer_->supports(slicer_);
        if (composing_)
        {
            composer_->configure(&slicer_, &session_->model(), &session_->piece_profile());
            reset_stream_plan();
        }

        // Piece analysis (stream plan S1): key, chords + nearings, motifs,
        // rhythm figures — from the judgment axis itself, dumped beside the
        // progress file for inspection and cached for the composer.
        std::vector<AnalysisOnset> analysis_onsets;
        analysis_onsets.reserve(flow_.onsets().size());
        for (const FlowController::Onset& onset : flow_.onsets())
        {
            AnalysisOnset entry;
            entry.qstamp = onset.qstamp;
            for (const std::string& id : onset.ids)
            {
                const int pitch = engine_->midi_pitch_for_element(id);
                if (pitch >= 0)
                    entry.pitches.push_back(pitch);
            }
            if (!entry.pitches.empty())
                analysis_onsets.push_back(std::move(entry));
        }
        std::optional<int> notated_fifths;
        if (has_model_ && !model_.parts.empty())
        {
            for (const auto& measure : model_.parts[0].measures)
            {
                if (measure.key)
                {
                    notated_fifths = measure.key->fifths;
                    break;
                }
            }
        }
        session_->set_piece_profile(
            analyze_piece(analysis_onsets, quarters_per_bar_, notated_fifths));
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

ScoreHost::FlowBuildResult ScoreHost::build_flow_from_engine(std::string& error)
{
    // The whole-piece flow build (Clock conveyor, the `mono` strip, and the
    // initial engraving) shares its extraction with a rolling window; only the
    // transport policy differs. This path leaves the mode/marking to the pump
    // and enter_gate_mode, so it does not use engrave_window's Roll config.
    EngraveParams params;
    params.pixel_scale = ui_scale();
    params.proportional_spacing = proportional_spacing_;
    params.spacing_linear = spacing_linear_override_;
    params.spacing_non_linear = spacing_non_linear_override_;
    EngravedWindow engraved;
    const EngraveResult result = engrave_loaded(*engine_, params, engraved, error);
    if (result == EngraveResult::InterpretFailed)
        return FlowBuildResult::InterpretFailed;
    // Show the strip even when the transport join fails (paged fallback).
    if (engraved.strip)
    {
        strip_ = std::move(engraved.strip);
        highlight_.build(*strip_);
    }
    if (result != EngraveResult::Ok)
        return FlowBuildResult::TransportFailed;
    flow_ = std::move(engraved.flow);
    note_palette_ = std::move(engraved.palette);
    waterfall_notes_ = std::move(engraved.waterfall);
    for (const auto& [id, palette] : note_palette_)
        highlight_.set_guidance(id, palette);
    return FlowBuildResult::Ok;
}

std::optional<ScoreHost::WindowSlice> ScoreHost::build_window_slice(int first_bar)
{
    // `first_bar` indexes STREAM SLOTS when the composer drives the program
    // (S3); with the composer inactive it is a plain source-bar index. This is
    // the cheap main-thread half of a window advance (slicing + composer
    // planning); the heavy Verovio engrave then runs sync or on the worker.
    const int window_span = 1 + kWindowHistoryBars + kWindowAheadBars;
    WindowSlice slice;
    if (composing_)
    {
        const int available = composer_->ensure(stream_program_, first_bar + window_span);
        first_bar = std::clamp(first_bar, 0, std::max(0, available - 1));
        slice.count = std::min(window_span, available - first_bar);
        std::vector<SourceSlicer::StreamBar> items;
        items.reserve(static_cast<size_t>(slice.count));
        for (int slot = first_bar; slot < first_bar + slice.count; ++slot)
        {
            const StreamBarPlan& plan = stream_program_.plan(slot);
            if (!plan.reason.empty() && slot > last_logged_plan_slot_)
                DRAXUL_LOG_INFO(
                    LogCategory::App, "stream: slot %d = %s", slot, plan.reason.c_str());
            last_logged_plan_slot_ = std::max(last_logged_plan_slot_, slot);
            SourceSlicer::StreamBar item;
            if (plan.kind == StreamBarPlan::Kind::Drill)
                item.measure_xml = plan.drill_xml;
            else
                item.source_bar = plan.source_bar;
            items.push_back(std::move(item));
        }
        slice.xml = slicer_.window_xml_for(items, stream_program_.plan(first_bar).source_bar);
        slice.stream_offset_q = stream_program_.slot_start_q(first_bar);
    }
    else
    {
        const int total = slicer_.bar_count();
        first_bar = std::clamp(first_bar, 0, std::max(0, total - 1));
        slice.count = std::min(window_span, total - first_bar);
        slice.xml = slicer_.window_xml(first_bar, slice.count);
        slice.stream_offset_q = slicer_.bar_start_q(first_bar);
    }
    slice.first_bar = first_bar;
    if (slice.xml.empty())
        return std::nullopt;
    return slice;
}

bool ScoreHost::rebuild_window(
    int first_bar, double stream_position_q, bool carry, bool preserve_tempo)
{
    const auto swap_start = std::chrono::steady_clock::now();
    auto slice = build_window_slice(first_bar);
    if (!slice)
    {
        if (engraver_)
            engraver_->cancel();
        pending_window_install_.reset();
        async_engrave_in_flight_ = false;
        DRAXUL_LOG_WARN(LogCategory::App, "score: window slice empty, monolithic strip");
        stream_windowed_ = false;
        flow_dirty_ = true;
        return false;
    }

    EngraveParams params;
    params.pixel_scale = ui_scale();
    params.marking_qpm = piece_marking_qpm_;
    params.lock_tempo = lock_tempo_;
    params.proportional_spacing = proportional_spacing_;
    params.spacing_linear = spacing_linear_override_;
    params.spacing_non_linear = spacing_non_linear_override_;

    // Startup performs one synchronous build before interactive frames exist.
    // Every later rebuild uses the latest-wins worker and leaves the current
    // engraving visible until its replacement is ready.
    if (engraver_ && initial_window_installed_)
    {
        WindowEngraver::Job job;
        job.window_xml = std::move(slice->xml);
        job.params = params;
        job.first_bar = slice->first_bar;
        job.count = slice->count;
        job.stream_offset_q = slice->stream_offset_q;
        const WindowEngraver::RequestId request_id = engraver_->submit(std::move(job));
        if (request_id == 0)
            return false;
        pending_window_install_ = PendingWindowInstall{
            request_id, stream_position_q, carry, preserve_tempo, false
        };
        async_engrave_in_flight_ = true;
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: queued window generation %llu for bars %d..%d",
            static_cast<unsigned long long>(request_id), slice->first_bar,
            slice->first_bar + slice->count - 1);
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
        return true;
    }

    EngravedWindow engraved;
    std::string error;
    if (engrave_window(*engine_, slice->xml, params, engraved, error) != EngraveResult::Ok)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: window build failed (%s), monolithic strip",
            error.c_str());
        stream_windowed_ = false;
        flow_dirty_ = true; // reload the full piece on the next pump
        return false;
    }
    install_window(std::move(engraved), slice->first_bar, slice->count, slice->stream_offset_q,
        stream_position_q, carry, preserve_tempo);
    const double swap_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - swap_start)
                               .count();
    DRAXUL_LOG_DEBUG(LogCategory::App, "score: window -> bars %d..%d (%.1f ms, sync)",
        slice->first_bar, slice->first_bar + slice->count - 1, swap_ms);
    return true;
}

void ScoreHost::install_window(EngravedWindow&& engraved, int first_bar, int count,
    double stream_offset_q, double stream_position_q, bool carry, bool preserve_tempo)
{
    // engrave_window already set the Roll transport, marking and tempo lock on
    // the incoming flow; carry-over comes from the OUTGOING transport, captured
    // before the move replaces it. Everything here is cheap — no Verovio — so
    // the swap costs a frame at most, not a ~100ms freeze.
    const FlowController::CarryState carried = flow_.carry_state();
    const double outgoing_tempo_qpm = flow_.tempo_qpm();
    const bool was_playing = flow_.playing();

    strip_ = std::move(engraved.strip);
    flow_ = std::move(engraved.flow);
    note_palette_ = std::move(engraved.palette);
    waterfall_notes_ = std::move(engraved.waterfall);
    rebuild_highlight_from_palette();

    window_first_bar_ = first_bar;
    window_bar_count_ = count;
    stream_offset_q_ = stream_offset_q;
    engine_holds_window_ = true;
    initial_window_installed_ = true;

    if (carry)
    {
        flow_.restore_carry(carried);
        const double local_position = stream_position_q - stream_offset_q_;
        const double window_end_q = composing_
            ? stream_program_.slot_start_q(first_bar + count)
            : slicer_.bar_start_q(first_bar + count);
        verdict_archive_.replay(stream_offset_q_, window_end_q,
            [this](double local_q, int pitch, NoteVerdict verdict) {
                flow_.preset_verdict(local_q, pitch, verdict);
            });
        flow_.fast_forward_resolved(local_position);
        flow_.seek(local_position);
    }
    else if (preserve_tempo)
    {
        flow_.set_tempo_qpm(outgoing_tempo_qpm);
    }
    if (was_playing)
        flow_.play();
    else
        flow_.pause();
    apply_verdict_update(); // repaint carried verdicts on the fresh strip
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::rebuild_highlight_from_palette()
{
    if (!strip_)
        return;
    highlight_.build(*strip_);
    for (const auto& [id, palette] : note_palette_)
        highlight_.set_guidance(id, palette);
}

void ScoreHost::restart_stream(bool keep_tempo)
{
    const double tempo = flow_.tempo_qpm();
    verdict_archive_.clear();
    reset_stream_plan();
    rebuild_window(0, 0.0, /*carry=*/false, /*preserve_tempo=*/keep_tempo);
    if (keep_tempo && !lock_tempo_)
        flow_.set_tempo_qpm(tempo);
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::reset_stream_plan()
{
    stream_program_.clear();
    composer_->reset();
    last_logged_plan_slot_ = -1;
}

void ScoreHost::reengrave_flow_in_place()
{
    stream_scale_ref_ = 0.0f;
    stream_scale_ = 0.0f;
    if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
        rebuild_window(window_first_bar_, stream_position_q(), /*carry=*/true);
    else
        flow_dirty_ = true;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::maybe_advance_stream()
{
    if (!stream_active() || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    // A swap is already being engraved in the background; it installs in pump()
    // (poll_async_engrave). Don't queue a second one.
    if (async_engrave_in_flight_)
        return;
    const double stream_q = stream_position_q();
    int playhead_bar = 0;
    if (composing_)
    {
        if (composer_->finished()
            && window_first_bar_ + window_bar_count_ >= stream_program_.size())
            return; // the program is complete and the window reaches its end
        playhead_bar = stream_program_.slot_at(stream_q);
    }
    else
    {
        if (window_first_bar_ + window_bar_count_ >= slicer_.bar_count())
            return; // the window tail already reaches the final bar
        playhead_bar = slicer_.bar_at(stream_q);
    }
    // Advance only when the playhead comes within the viewport's look-ahead
    // of the window's tail — NOT every bar. The background engrave is only ~100
    // ms while the window still has ~kWindowAheadBars of runway ahead, so the
    // old window keeps playing smoothly until the fresh one installs. It also
    // keeps kWindowHistoryBars behind the playhead so the scroll never clamps.
    const int window_end = window_first_bar_ + window_bar_count_;
    if (playhead_bar <= window_first_bar_ + kWindowHistoryBars
        || playhead_bar < window_end - stream_ahead_needed_)
        return;

    // Slice + plan on the main thread (cheap), then hand the heavy Verovio
    // engrave to the worker. The current window goes on playing until poll()
    // installs the result — no freeze, no wall-time catch-up.
    auto slice = build_window_slice(playhead_bar - kWindowHistoryBars);
    if (!slice)
        return;
    if (engraver_)
    {
        WindowEngraver::Job job;
        job.window_xml = std::move(slice->xml);
        job.params.pixel_scale = ui_scale();
        job.params.marking_qpm = piece_marking_qpm_;
        job.params.lock_tempo = lock_tempo_;
        job.params.proportional_spacing = proportional_spacing_;
        job.params.spacing_linear = spacing_linear_override_;
        job.params.spacing_non_linear = spacing_non_linear_override_;
        job.first_bar = slice->first_bar;
        job.count = slice->count;
        job.stream_offset_q = slice->stream_offset_q;
        const WindowEngraver::RequestId request_id = engraver_->submit(std::move(job));
        if (request_id == 0)
            return;
        pending_window_install_ = PendingWindowInstall{
            request_id, stream_q, true, false, true
        };
        async_engrave_in_flight_ = true;
        return;
    }
    // No worker (creation failed): fall back to the synchronous swap, and don't
    // charge its wall-time to the transport (else the playhead jumps to catch
    // up across the freeze).
    const auto rebuild_start = std::chrono::steady_clock::now();
    rebuild_window(playhead_bar - kWindowHistoryBars, stream_q, /*carry=*/true);
    last_pump_ += std::chrono::steady_clock::now() - rebuild_start;
}

void ScoreHost::poll_async_engrave()
{
    if (!engraver_)
        return;
    auto done = engraver_->poll();
    if (!done)
        return;
    handle_async_engrave_done(std::move(*done));
}

void ScoreHost::handle_async_engrave_done(WindowEngraver::Done done)
{
    if (!pending_window_install_)
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: host discarded unexpected window generation %llu",
            static_cast<unsigned long long>(done.request_id));
        async_engrave_in_flight_ = false;
        return;
    }
    if (done.request_id != pending_window_install_->request_id)
    {
        // A superseded generation must not retire the newest pending install.
        // WindowEngraver normally filters these itself, but retaining the
        // latest request here makes the host robust to delayed/fake workers.
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: host discarded stale window generation %llu; waiting for %llu",
            static_cast<unsigned long long>(done.request_id),
            static_cast<unsigned long long>(pending_window_install_->request_id));
        return;
    }
    const PendingWindowInstall install = *pending_window_install_;
    pending_window_install_.reset();
    async_engrave_in_flight_ = false;
    if (!done.ok)
    {
        // A failed speculative advance retains the old window until the
        // monolithic fallback can be rebuilt. Interactive restyling failures
        // simply keep the current valid engraving visible.
        if (install.fallback_to_monolith_on_error)
        {
            stream_windowed_ = false;
            flow_dirty_ = true;
        }
        return;
    }
    // If the player left the rolling window while the engrave ran (paged view,
    // exited the game), drop the now-stale result rather than forcing Roll back.
    if (view_mode_ != ViewMode::Flow
        || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    // Install at the CURRENT playhead (the old window kept advancing while the
    // engrave ran); carry replays the transport/verdicts onto the fresh strip.
    const int first_bar = done.first_bar;
    const int count = done.count;
    const auto install_start = std::chrono::steady_clock::now();
    const double install_position_q
        = install.carry ? stream_position_q() : install.stream_position_q;
    install_window(std::move(done.window), first_bar, count, done.stream_offset_q,
        install_position_q, install.carry, install.preserve_tempo);
    const double install_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - install_start)
                                  .count();
    DRAXUL_LOG_DEBUG(LogCategory::App, "score: window -> bars %d..%d (%.1f ms install, async)",
        first_bar, first_bar + count - 1, install_ms);
}

void ScoreHost::toggle_flow_mode()
{
    if (!engine_ || !engine_->is_loaded())
        return;
    if (engraver_)
        engraver_->cancel();
    pending_window_install_.reset();
    async_engrave_in_flight_ = false;
    if (view_mode_ == ViewMode::Paged)
    {
        view_mode_ = ViewMode::Flow;
        flow_dirty_ = true; // re-engrave as the strip on the next pump
        // Returning to the runner: if the piece supports the rolling Roll
        // window, re-arm it (the flow build re-enters it via start_in_gate_);
        // otherwise this falls through to the whole-piece conveyor.
        if (game_mode_ == FlowController::TransportMode::Roll && stream_windowed_
            && slicer_.ready())
            start_in_gate_ = true;
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
    // Wrong-note marking can be turned off to read the pure notation (the flow
    // controller still tracks hits/misses for scoring — only the on-sheet
    // cross is suppressed). Verdicts never recolor the note; a wrong one is
    // flagged by a cross the renderer draws over the Missed state.
    if (!mark_mistakes_)
        return;
    for (const auto& [id, verdict] : update.changes)
    {
        highlight_.set_state(id,
            verdict == FlowController::NoteVerdict::Missed ? ScoreHighlightState::State::Missed
                                                           : ScoreHighlightState::State::Correct);
    }
}

bool ScoreHost::set_gate_input(
    GateInput input, double bot_pace_qpm, double bot_accuracy, int midi_port)
{
    PlayerInputRig::Selection selection;
    selection.kind = input == GateInput::Bot ? PlayerInputRig::Kind::Bot
        : input == GateInput::Mic            ? PlayerInputRig::Kind::Mic
        : input == GateInput::Midi           ? PlayerInputRig::Kind::Midi
                                             : PlayerInputRig::Kind::Keyboard;
    selection.bot_pace_qpm = bot_pace_qpm;
    selection.bot_accuracy = bot_accuracy;
    selection.midi_port = midi_port;
    const bool engaged = input_rig_.select(selection, flow_);
    // The keyboard is the instrument: make it heard (play-thru needs the
    // output stream open even when the metronome is off).
    if (engaged && input_rig_.kind() == PlayerInputRig::Kind::Midi)
        audio_->ensure_output_stream();
    return engaged;
}

void ScoreHost::enter_gate_mode(GateInput input, double bot_pace_qpm, double bot_accuracy)
{
    if (!flow_.gates_ready())
        return;
    if (game_mode_ == FlowController::TransportMode::Roll && stream_windowed_ && slicer_.ready())
    {
        // The runner plays on the rolling window from stream bar 0, with
        // a fresh program (mastery persists in the player model, not here).
        verdict_archive_.clear();
        reset_stream_plan();
        if (!rebuild_window(0, 0.0, /*carry=*/false))
            flow_.set_mode(game_mode_); // fell back to the monolithic strip
        else if (async_engrave_in_flight_)
            flow_.set_mode(game_mode_); // old strip stays interactive while replacing it
    }
    else
    {
        flow_.set_mode(game_mode_);
    }
    highlight_.clear_lit();
    apply_verdict_update(); // consume the reset
    set_gate_input(input, bot_pace_qpm, bot_accuracy);
    // The gate transport starts immediately for every input: it just glides
    // to the first onset and WAITS there. Space pauses/resumes.
    flow_.play();
    if (game_mode_ == FlowController::TransportMode::Roll)
        begin_progress_session();
    last_logged_gate_ = 0;
    logged_gate_end_ = false;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::exit_gate_mode()
{
    end_progress_session();
    if (engraver_)
        engraver_->cancel();
    pending_window_install_.reset();
    async_engrave_in_flight_ = false;
    input_rig_.clear();
    flow_.set_mode(FlowController::TransportMode::Clock);
    if (stream_active())
    {
        // The engine holds a window document; the clock conveyor wants the
        // whole piece back (relayout_flow reloads the source).
        verdict_archive_.clear();
        flow_dirty_ = true;
    }
    highlight_.clear_lit();
    apply_lit_update(); // consume the reset; re-light anything at q <= 0
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

bool ScoreHost::handle_gate_key(int keycode)
{
    if (flow_.mode() == FlowController::TransportMode::Clock)
        return false;
    if (keycode == SDLK_ESCAPE)
    {
        exit_gate_mode();
        return true;
    }
    // `i` switches the human input source mid-session (mic <-> keyboard)
    // without touching verdicts, score, or the transport.
    const bool mic_live = input_rig_.kind() == PlayerInputRig::Kind::Mic;
    if (keycode == SDLK_I
        && (input_rig_.kind() == PlayerInputRig::Kind::Keyboard || mic_live))
    {
        const GateInput next = mic_live ? GateInput::Keyboard : GateInput::Mic;
        const bool engaged = set_gate_input(next, 0.0, 1.0);
        DRAXUL_LOG_INFO(LogCategory::App, "score: gate input -> %s",
            input_rig_.kind() == PlayerInputRig::Kind::Mic ? "microphone" : "keyboard");
        if (next == GateInput::Mic && engaged && !flow_.playing())
            flow_.play(); // the piano is the interface; don't demand Space
        return true;
    }
    if (input_rig_.kind() != PlayerInputRig::Kind::Keyboard)
        return false; // bot/mic/midi session: the piano row doesn't inject notes

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
        input_rig_.feed_keyboard_note(anchor + semitone, now_seconds());
        return true;
    }
    if (keycode == SDLK_RETURN)
    {
        for (const int pitch : expected)
            input_rig_.feed_keyboard_note(pitch, now_seconds());
        return true;
    }
    if (keycode == SDLK_BACKSPACE)
    {
        // Fluff exactly ONE note of the gate: a randomly chosen required
        // pitch is replaced by an adjacent wrong note and the rest play
        // correctly, so the gate resolves in one press with per-note
        // green/red verdicts — the error data the practice generator wants.
        static uint32_t rng = 0x9E3779B9u;
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        if (expected.empty())
        {
            input_rig_.feed_keyboard_note(anchor + 1, now_seconds());
            return true;
        }
        const size_t fluffed = rng % expected.size();
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (i != fluffed)
                input_rig_.feed_keyboard_note(expected[i], now_seconds());
        }
        const int direction = (rng & 0x10000u) != 0 ? 1 : -1;
        int wrong = expected[fluffed] + direction;
        while (std::find(expected.begin(), expected.end(), wrong) != expected.end())
            wrong += direction;
        input_rig_.feed_keyboard_note(wrong, now_seconds());
        return true;
    }
    return false;
}

double ScoreHost::quarters_per_measure_from_model() const
{
    if (!has_model_ || model_.parts.empty())
        return 0.0;
    for (const auto& measure : model_.parts[0].measures)
    {
        if (measure.time)
            return measure.time->measure_duration().to_double() * 4.0;
    }
    return 0.0;
}

void ScoreHost::begin_progress_session()
{
    if (!session_->begin_session())
        return;
    // Resume at yesterday's pace: the stored tempo informs the start, still
    // clamped to the marking band. Fresh pieces keep the 60% default. The
    // tempo lock overrides this — it holds the marking regardless.
    if (!lock_tempo_ && session_->model().last_tempo_frac() > 0.0 && flow_.ready())
        flow_.set_tempo_qpm(flow_.marking_qpm() * session_->model().last_tempo_frac());
}

void ScoreHost::end_progress_session()
{
    session_->end_session(
        flow_.marking_qpm() > 0.0 ? flow_.tempo_qpm() / flow_.marking_qpm() : 0.0);
}

void ScoreHost::clear_piece_progress()
{
    session_->clear_progress();
    begin_progress_session(); // session_active_ is false after clear — starts fresh
    session_->save(/*final_flush=*/false); // overwrite the file with the cleared model
    // Restart the stream from the top so the composer re-plans against a blank
    // slate. A cleared record is the one restart that RESETS the tempo — a
    // fresh learner starts at the 60% ramp again.
    restart_stream(/*keep_tempo=*/false);
    DRAXUL_LOG_INFO(LogCategory::App, "score: cleared progress for this piece");
}

int ScoreHost::approx_measure() const
{
    if (!flow_.ready() || !has_model_)
        return 0;
    const double quarters_per_measure = quarters_per_measure_from_model();
    if (quarters_per_measure <= 0.0)
        return 0;
    if (composing_ && stream_active())
    {
        const int slot = stream_program_.slot_at(stream_position_q());
        if (slot < stream_program_.size())
            return stream_program_.plan(slot).source_bar + 1;
    }
    return static_cast<int>(stream_position_q() / quarters_per_measure) + 1;
}

void ScoreHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    // Clamp long stalls (first frame, app pauses) to one frame's worth.
    const double dt = std::clamp(std::chrono::duration<double>(now - last_pump_).count(), 0.0, 0.1);
    last_pump_ = now;

    // Poll regardless of transport/view state: paused spacing changes and
    // invalidated jobs must still retire. Synchronous main-engine layouts are
    // deferred while the worker toolkit is active, preserving the existing
    // no-concurrent-Verovio invariant without blocking this frame.
    poll_async_engrave();
    const bool background_engrave_active = engraver_ && engraver_->busy();
    if (view_mode_ == ViewMode::Flow && flow_dirty_ && !background_engrave_active)
        relayout_flow();
    if (view_mode_ == ViewMode::Paged && layout_dirty_ && !background_engrave_active)
        relayout();

    if (view_mode_ == ViewMode::Flow && flow_.playing())
    {
        const double position_before_q = flow_.position_q();
        flow_.advance(dt);
        if (audio_->wants_pump())
            audio_->pump(position_before_q, flow_.position_q(), dt, quarters_per_bar_, flow_);
        if (flow_.mode() != FlowController::TransportMode::Clock)
        {
            if (input_rig_.mic_failed())
            {
                DRAXUL_LOG_WARN(LogCategory::App, "score: %s — falling back to keyboard input",
                    input_rig_.mic_error().c_str());
                set_gate_input(GateInput::Keyboard, 0.0, 1.0);
            }
            if (input_rig_.active())
            {
                std::vector<PlayerNoteEvent> events;
                input_rig_.input()->poll(now_seconds(), events);
                if (!events.empty())
                    flow_.judge(events);
            }
            // MIDI play-thru: most controllers are silent — voice the keys
            // through the selected instrument so playing is audible. Offs
            // matter for the piano (the damper stops the string).
            if (input_rig_.kind() == PlayerInputRig::Kind::Midi)
            {
                std::vector<MidiVoiceEvent> voiced;
                input_rig_.take_midi_voice_events(voiced);
                audio_->voice_midi_events(voiced);
            }
            apply_verdict_update();

            // Player memory: verdicts archive on the STREAM axis (for
            // window repaints), then translate through the program's
            // provenance — a review of bar 3 trains bar 3's statistics,
            // and drill outcomes train pitch/chord stats only.
            for (FlowController::NoteOutcome outcome : flow_.take_note_outcomes())
            {
                const double stream_q = outcome.onset_q + stream_offset_q_;
                if (!outcome.stray)
                    verdict_archive_.record(stream_q, outcome.pitch, outcome.verdict);
                if (composing_ && !outcome.stray)
                {
                    const StreamProgram::SourceRef ref = stream_program_.source_at(stream_q);
                    outcome.onset_q
                        = ref.drill ? PlayerModel::kDrillOnsetSentinel : ref.source_q;
                }
                else
                {
                    outcome.onset_q = stream_q;
                }
                session_->model().apply(outcome);
                session_->mark_dirty();
            }
            for (FlowController::ChordOutcome outcome : flow_.take_chord_outcomes())
            {
                outcome.onset_q += stream_offset_q_;
                session_->model().apply(outcome);
                session_->mark_dirty();
            }
            session_->flush_at_bar(
                static_cast<int>(stream_position_q() / quarters_per_bar_));
            maybe_advance_stream();

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
    if (engraver_ && engraver_->busy())
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    if (view_mode_ == ViewMode::Flow && flow_.playing())
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    return std::nullopt;
}

void ScoreHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    const float pixel_scale = ui_scale();
    const bool retain_flow_while_paged_layout_waits = view_mode_ == ViewMode::Paged
        && layout_dirty_ && (!pages_ || pages_->empty()) && engraver_ && engraver_->busy();
    if ((view_mode_ == ViewMode::Flow || retain_flow_while_paged_layout_waits) && strip_
        && strip_->canvas_size.y > 0.0f)
    {
        // The conveyor: the whole piece as one strip on a full-width band,
        // scrolled so the playhead anchor tracks the transport position.
        auto strip = strip_;
        const ScoreHighlightState* highlight = &highlight_;
        const float vw = static_cast<float>(viewport_.pixel_size.x);
        const float vh = static_cast<float>(viewport_.pixel_size.y);
        FlowBand band = flow_band();
        const bool streaming = stream_active()
            && flow_.mode() == FlowController::TransportMode::Roll;
        // Layout (top to bottom): a FIXED score band (score_height_frac_ of the
        // pane height), then the waterfall, then the stumpy keyboard. The sheet
        // scales (locked) to fill the score band and is clipped to it, so it
        // never resizes or jumps as the window scrolls under the playhead.
        const bool show_wf = streaming && show_waterfall_ && !waterfall_notes_.empty();
        const float score_region_h
            = std::clamp(vh * score_height_frac_, 96.0f * pixel_scale, vh * 0.9f);
        const float keyboard_h = streaming ? std::min(vw / 52.0f * 3.4f, vh * 0.16f) : 0.0f;
        const float keyboard_y = vh - keyboard_h;
        const float waterfall_h = show_wf ? std::max(0.0f, keyboard_y - score_region_h) : 0.0f;
        const float waterfall_top = keyboard_y - waterfall_h;
        if (streaming)
        {
            // Fit the scale to the TALLEST engraving (the full-piece extent
            // seeds it; a taller fabricated bar grows it once). The reference
            // only grows, so the sheet fills the band without ever clipping
            // off the bottom or jittering. Re-fit on viewport/zoom/waterfall/
            // band-fraction change, or when the reference grows.
            const float want_ref = std::max(stream_scale_ref_, strip->canvas_size.y);
            const int wf_key = show_wf ? 1 : 0;
            if (stream_scale_ <= 0.0f || stream_scale_vw_ != viewport_.pixel_size.x
                || stream_scale_zoom_ != zoom_ || stream_scale_wf_ != wf_key
                || stream_scale_frac_ != score_height_frac_ || stream_scale_ref_ != want_ref)
            {
                stream_scale_ref_ = want_ref;
                const float usable = score_region_h * 0.9f;
                stream_scale_ = (usable / std::max(1.0f, want_ref)) * std::max(0.25f, zoom_);
                stream_scale_vw_ = viewport_.pixel_size.x;
                stream_scale_zoom_ = zoom_;
                stream_scale_wf_ = wf_key;
                stream_scale_frac_ = score_height_frac_;
                // Anchor the playhead at ~30% across, but never past the
                // history the window supplies (history_bars / visible_bars) —
                // otherwise the scroll clamps left and the playhead snaps back
                // when the window advances.
                const double avg_bar_canvas
                    = strip->canvas_size.x / std::max(1, window_bar_count_);
                const double visible_bars
                    = (static_cast<double>(vw) / std::max(0.001f, stream_scale_))
                    / std::max(1.0, avg_bar_canvas);
                stream_anchor_ = static_cast<float>(std::min(0.30,
                    static_cast<double>(kWindowHistoryBars) / std::max(1.0, visible_bars)));
                // The look-ahead the viewport shows to the right of the
                // playhead (+1 buffer) — the window advances when it drops to
                // this, so the sheet never runs out of notes ahead.
                stream_ahead_needed_ = std::clamp(
                    static_cast<int>(std::ceil((1.0 - stream_anchor_) * visible_bars)) + 1, 1,
                    kWindowAheadBars);
            }
            // Fixed visual band, independent of the current window's canvas
            // extent — so the sheet backdrop and playhead never move.
            band.target_h = score_region_h * 0.9f;
            band.strip_y = (score_region_h - band.target_h) * 0.5f;
        }
        const float target_h = band.target_h;
        const float scale
            = streaming ? stream_scale_ : (target_h / std::max(1.0f, strip->canvas_size.y));
        const double anchor_frac = streaming ? stream_anchor_ : 0.30;
        const double scroll_canvas = flow_.scroll_x(static_cast<double>(vw) / scale, anchor_frac);
        const float origin_x = static_cast<float>(-scroll_canvas * scale);
        const float strip_y = band.strip_y;
        const float playhead_x = static_cast<float>((flow_.x_at(flow_.position_q()) - scroll_canvas) * scale);
        const bool waiting = flow_.waiting();

        // The sheet is already colored (build_flow_from_engine paints every
        // note its spelling's color, once per engraving). Here we only pick
        // the KEYS to light. Without the waterfall the keyboard is the
        // anticipatory guide: onsets whose hit window holds the playhead,
        // faded by trailing clean plays (3 clean = invisible), drills always
        // on. With the waterfall present the falling blocks do the
        // anticipating, so the keys instead light on the beat (below) — this
        // is what keeps a key from lighting before its block lands.
        std::vector<KeyboardLit> lit;
        if (streaming && !show_wf)
        {
            const double position = flow_.position_q();
            const auto& onsets = flow_.onsets();
            const auto& gates = flow_.gates();
            for (size_t i = 0; i < onsets.size() && i < gates.size(); ++i)
            {
                const double q = onsets[i].qstamp;
                if (q > position + FlowController::kRollEarlyWindowQ)
                    break;
                if (q < position - FlowController::kRollLateWindowQ)
                    continue;
                const double stream_q = q + stream_offset_q_;
                int trailing = 0;
                bool drill = false;
                if (composing_)
                {
                    const StreamProgram::SourceRef ref = stream_program_.source_at(stream_q);
                    drill = ref.drill;
                    if (!ref.drill)
                        trailing = session_->model().onset_trailing_correct(ref.source_q);
                }
                else
                {
                    trailing = session_->model().onset_trailing_correct(stream_q);
                }
                const float need = drill
                    ? 1.0f
                    : std::clamp(1.0f - static_cast<float>(trailing) / 3.0f, 0.0f, 1.0f);
                if (need <= 0.0f)
                    continue;
                for (const FlowController::GateNote& note : gates[i].notes)
                {
                    if (note.verdict == FlowController::NoteVerdict::Pending
                        && !note.auto_satisfied)
                    {
                        const auto found = note_palette_.find(note.id);
                        const int palette = found != note_palette_.end()
                            ? found->second
                            : guidance_palette_index(note.pitch);
                        lit.push_back({ note.pitch, need, palette });
                    }
                }
            }
        }
        // Waterfall blocks + the keyboard's playback lighting. A block falls
        // so its bottom edge reaches the keys exactly as the transport crosses
        // the note's onset; the matching key lights full-bright while the note
        // sounds and dims when it ends. A note holds for note_gate_ of its
        // notated length (articulation) with a floor of a couple pixels of
        // daylight, so successive notes on one key are visibly separate and
        // never light two-at-once in a run.
        struct WaterfallBlock
        {
            float x = 0.0f, w = 0.0f, y0 = 0.0f, y1 = 0.0f;
            int palette = 0;
        };
        std::vector<WaterfallBlock> blocks;
        if (show_wf)
        {
            const double position = flow_.position_q();
            const float ppb
                = waterfall_h / static_cast<float>(std::max(1.0, waterfall_beats_));
            const float white_w = vw / 52.0f;
            const float gap = 2.0f * pixel_scale; // guaranteed daylight
            blocks.reserve(waterfall_notes_.size());
            for (const WaterfallNote& n : waterfall_notes_)
            {
                const float span = static_cast<float>(n.duration_q) * ppb;
                const float sounding
                    = static_cast<float>(n.duration_q * note_gate_) * ppb;
                const float height = std::max(1.0f, std::min(sounding, span - gap));
                const float bottom
                    = keyboard_y - static_cast<float>(n.onset_q - position) * ppb;
                const float top = bottom - height;
                if (bottom <= waterfall_top || top >= keyboard_y)
                    continue; // fully above the zone (future) or already played
                const float y0 = std::max(top, waterfall_top);
                const float y1 = std::min(bottom, keyboard_y);
                if (y1 - y0 < 1.0f)
                    continue;
                const float cx = keyboard_key_center_x(n.midi, 0.0f, vw);
                const float bw
                    = keyboard_is_black(n.midi) ? white_w * 0.52f : white_w * 0.74f;
                blocks.push_back({ cx - bw * 0.5f, bw, y0, y1, n.palette });
                // Light the key only while the note is sounding (the gated
                // length), so it dims before the next onset in a run.
                if (position + 1e-6 >= n.onset_q
                    && position < n.onset_q + n.duration_q * note_gate_)
                    lit.push_back({ n.midi, 1.0f, n.palette });
            }
        }
        const float keyboard_alpha = streaming ? 1.0f : 0.0f;
        const bool split_acc = split_accidentals_;

        const float score_clip_h = streaming ? score_region_h : vh;

        nanovg_pass_->set_draw_callback(
            [strip, highlight, pixel_scale, vw, target_h, scale, origin_x, strip_y, playhead_x,
                waiting, lit, keyboard_alpha, keyboard_y, keyboard_h, streaming, show_wf, blocks,
                waterfall_top, split_acc, score_clip_h](NVGcontext* vg, int w, int h) {
                fill_backdrop(vg, w, h);
                const float band_pad = 18.0f * pixel_scale;
                // The sheet, notes, and playhead are clipped to the fixed score
                // band so a tall window (ledger lines) can't spill into the
                // waterfall below.
                nvgSave(vg);
                nvgScissor(vg, 0.0f, 0.0f, static_cast<float>(vw), score_clip_h);
                draw_page_sheet(vg, -48.0f * pixel_scale, strip_y - band_pad,
                    vw + 96.0f * pixel_scale, target_h + 2.0f * band_pad, pixel_scale);
                const ScoreTextFonts fonts = ensure_score_text_fonts(vg);
                render_draw_list(
                    vg, *strip, { origin_x, strip_y }, scale, fonts, highlight, split_acc);
                // Playhead: amber while rolling, teal while awaiting the player.
                nvgBeginPath(vg);
                nvgRect(vg, playhead_x - 1.0f * pixel_scale, strip_y - band_pad,
                    2.0f * pixel_scale, target_h + 2.0f * band_pad);
                nvgFillColor(vg,
                    waiting ? nvgRGBA(24, 140, 165, 200) : nvgRGBA(217, 115, 20, 170));
                nvgFill(vg);
                nvgRestore(vg);
                // Waterfall: colored blocks falling to their keys, clipped to
                // the band between the score and the keyboard.
                if (show_wf)
                {
                    nvgSave(vg);
                    nvgScissor(vg, 0.0f, waterfall_top, static_cast<float>(vw),
                        keyboard_y - waterfall_top);
                    for (const auto& b : blocks)
                    {
                        const unsigned char* c
                            = kGuidancePalette[b.palette % kGuidancePaletteSize];
                        nvgBeginPath(vg);
                        nvgRoundedRect(
                            vg, b.x, b.y0, b.w, b.y1 - b.y0, 3.0f * pixel_scale);
                        nvgFillColor(vg, nvgRGBA(c[0], c[1], c[2], 235));
                        nvgFill(vg);
                    }
                    nvgRestore(vg);
                    // The hit line where blocks meet the keys.
                    nvgBeginPath(vg);
                    nvgRect(vg, 0.0f, keyboard_y - 1.5f * pixel_scale,
                        static_cast<float>(vw), 1.5f * pixel_scale);
                    nvgFillColor(vg, nvgRGBA(255, 255, 255, 46));
                    nvgFill(vg);
                }
                if (streaming)
                    draw_piano_keyboard(vg, 0.0f, keyboard_y, vw, keyboard_h, lit,
                        keyboard_alpha, pixel_scale);
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
    // The debug inspector draws last, over the score.
    const auto imgui_now = std::chrono::steady_clock::now();
    const float imgui_dt = std::chrono::duration<float>(imgui_now - last_imgui_time_).count();
    last_imgui_time_ = imgui_now;
    render_debug_ui(imgui_dt);
    if (imgui_context_ != nullptr && imgui_backend_ != nullptr)
        frame.render_imgui(ImGui::GetDrawData(), imgui_context_);
    frame.flush_submit_chunk();
}

void ScoreHost::on_key(const KeyEvent& event)
{
    if (imgui_context_ != nullptr)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.mod & kModCtrl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.mod & kModShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.mod & kModAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (event.mod & kModSuper) != 0);
        const ImGuiKey imkey = sdl_scancode_to_imgui_key(event.scancode);
        if (imkey != ImGuiKey_None)
            io.AddKeyEvent(imkey, event.pressed);
        // The backtick always toggles the inspector, even with ImGui focused.
        if (event.pressed && event.keycode == SDLK_GRAVE)
        {
            show_debug_ui_ = !show_debug_ui_;
            if (callbacks_ != nullptr)
                callbacks_->request_frame();
            return;
        }
        if (io.WantCaptureKeyboard)
        {
            if (callbacks_ != nullptr)
                callbacks_->request_frame();
            return;
        }
    }
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
            game_mode_ = FlowController::TransportMode::Roll; // `g` = the game
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
            if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
            {
                restart_stream(/*keep_tempo=*/true);
            }
            else
            {
                flow_.rewind();
                apply_lit_update();
            }
            break;
        case SDLK_T:
            audio_->cycle_tick_level();
            break;
        case SDLK_P:
            audio_->toggle_audition();
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
    if (imgui_context_ != nullptr)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
        if (ImGui::GetIO().WantCaptureMouse)
        {
            if (callbacks_ != nullptr)
                callbacks_->request_frame();
            return;
        }
    }
    scroll_by(-event.delta.y * 40.0f * ui_scale());
}

void ScoreHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    int button = -1;
    switch (event.button)
    {
    case 1:
        button = 0; // left
        break;
    case 2:
        button = 2; // middle
        break;
    case 3:
        button = 1; // right
        break;
    default:
        break;
    }
    if (button >= 0)
        ImGui::GetIO().AddMouseButtonEvent(button, event.pressed);
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().AddMousePosEvent(
        static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreHost::on_text_input(const TextInputEvent& event)
{
    if (imgui_context_ == nullptr || event.text.empty())
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().AddInputCharactersUTF8(event.text.c_str());
}

void ScoreHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_backend_ = &host;
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    host.initialize_imgui_backend();
    host.rebuild_imgui_font_texture();
}

void ScoreHost::set_imgui_font(const std::string& path, float size_pixels)
{
    imgui_font_path_ = path;
    imgui_font_size_pixels_ = size_pixels;
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!imgui_font_path_.empty() && imgui_font_size_pixels_ > 0.0f)
        io.Fonts->AddFontFromFileTTF(imgui_font_path_.c_str(), imgui_font_size_pixels_);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    if (imgui_backend_ != nullptr)
        imgui_backend_->rebuild_imgui_font_texture();
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
        if (flow_.mode() != FlowController::TransportMode::Clock)
        {
            const bool roll = flow_.mode() == FlowController::TransportMode::Roll;
            status += !roll && flow_.waiting()
                ? "  WAIT"
                : (flow_.at_end() ? "  end" : (flow_.playing() ? "  >" : "  ||"));
            if (input_rig_.kind() == PlayerInputRig::Kind::Mic)
            {
                if (input_rig_.mic_ready())
                {
                    // Input level 0-9: the at-a-glance "it hears me" meter.
                    const int level = std::clamp(
                        static_cast<int>(input_rig_.mic_level() * 9.99f), 0, 9);
                    status += "  MIC" + std::to_string(level);
                }
                else
                {
                    status += "  MIC?"; // waiting on device / permission
                }
            }
            status += "  " + std::to_string(qpm) + "qpm (" + std::to_string(pct) + "%)";
            if (audio_->tick_level() != ScoreAudioController::TickLevel::Off)
                status += audio_->tick_level() == ScoreAudioController::TickLevel::Beats
                    ? "  tick"
                    : "  tick8";
            if (audio_->audition())
                status += "  notes";
            if (roll)
            {
                const int acc = static_cast<int>(std::lround(flow_.accuracy_ema() * 100.0));
                status += "  acc " + std::to_string(acc) + "%";
                if (composing_ && stream_active())
                {
                    const int slot = stream_program_.slot_at(stream_position_q());
                    if (slot < stream_program_.size())
                    {
                        const StreamBarPlan::Kind kind = stream_program_.plan(slot).kind;
                        if (kind == StreamBarPlan::Kind::Drill)
                            status += "  DRILL";
                        else if (kind == StreamBarPlan::Kind::Review)
                            status += "  REVIEW";
                    }
                }
            }
            status += "  score " + std::to_string(flow_.score());
            status += "  x" + std::to_string(flow_.streak());
            if (flow_.miss_count() > 0)
                status += "  miss " + std::to_string(flow_.miss_count());
            if (roll && flow_.wrong_count() > 0)
                status += "  wr " + std::to_string(flow_.wrong_count());
            return status;
        }
        status += flow_.playing() ? "  >" : (flow_.at_end() ? "  end" : "  ||");
        status += "  " + std::to_string(qpm) + "qpm (" + std::to_string(pct) + "%)";
        if (audio_->tick_level() != ScoreAudioController::TickLevel::Off)
            status += audio_->tick_level() == ScoreAudioController::TickLevel::Beats
                ? "  tick"
                : "  tick8";
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

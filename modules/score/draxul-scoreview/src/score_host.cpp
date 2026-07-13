#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/notation/musicxml_importer.h>
#include <draxul/runtime_path.h>
#include <draxul/scoreview/bot_player_input.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/progress_store.h>
#include <draxul/scoreview/score_render_nvg.h>
#include <draxul/scoreview/svg_score_interpreter.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <ctime>

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

std::string now_iso8601()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return buffer;
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

        // Player memory: the per-piece progress file, keyed by the source
        // bytes so renames don't lose history (stream plan S0).
        const std::filesystem::path progress_dir = ConfigDocument::default_path().parent_path() / "scoreview" / "progress";
        progress_path_ = progress_path(progress_dir, bytes);
        const std::string stored = load_progress(progress_path_);
        if (!stored.empty() && player_model_.deserialize(stored))
        {
            DRAXUL_LOG_INFO(LogCategory::App,
                "score: progress loaded — %zu session(s), %d notes, best tempo %d%%",
                player_model_.sessions().size(), player_model_.total_notes_judged(),
                static_cast<int>(std::lround(player_model_.best_tempo_frac() * 100.0)));
        }
        else if (!stored.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "score: progress file unreadable, starting fresh (%s)",
                progress_path_.string().c_str());
        }

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
            tick_level_ = TickLevel::Off;
        else if (command.find("tick8") != std::string::npos)
            tick_level_ = TickLevel::Eighths;
        else if (command.find("tick") != std::string::npos)
            tick_level_ = TickLevel::Beats;
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
    end_progress_session();
    if (tick_stream_ != nullptr)
    {
        SDL_DestroyAudioStream(tick_stream_);
        tick_stream_ = nullptr;
    }
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
    player_model_.set_piece(has_model_ && !model_.title.empty()
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
        // The composer needs a single-part source (fabricated drill bars
        // are written for the grand staff); multi-part pieces stream the
        // source verbatim.
        composing_ = stream_windowed_ && slicer_.ready() && slicer_.part_count() == 1;
        if (composing_)
            composer_.configure(&slicer_, &player_model_, &piece_profile_);

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
        piece_profile_ = analyze_piece(analysis_onsets, quarters_per_bar_, notated_fifths);
        DRAXUL_LOG_INFO(LogCategory::App,
            "score: analysis — key %s (%.2f), %zu chord(s), %zu motif(s), %zu figure(s)",
            key_name(piece_profile_.global_key.tonic_pc, piece_profile_.global_key.minor)
                .c_str(),
            piece_profile_.global_key.confidence, piece_profile_.chords.size(),
            piece_profile_.motifs.size(), piece_profile_.figures.size());
        if (!progress_path_.empty())
        {
            std::filesystem::path analysis_path = progress_path_;
            analysis_path.replace_extension(".analysis.json");
            std::string save_error;
            if (!save_progress_atomic(analysis_path, piece_profile_.serialize(), save_error))
                DRAXUL_LOG_WARN(
                    LogCategory::App, "score: analysis dump failed: %s", save_error.c_str());
        }
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
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    options.pixel_scale = ui_scale();
    engine_->set_options(options);

    auto strip = interpret_score_svg(engine_->render_page_svg(1), error);
    if (!strip)
        return FlowBuildResult::InterpretFailed;
    for (const std::string& warning : strip->warnings)
        DRAXUL_LOG_DEBUG(LogCategory::App, "score flow interpreter: %s", warning.c_str());

    auto shared_strip = std::make_shared<const ScoreDrawList>(std::move(*strip));
    auto timemap = parse_timemap(engine_->render_timemap(), error);
    const bool transport_ok = timemap.has_value() && flow_.build(*timemap, *shared_strip, error);
    strip_ = std::move(shared_strip);
    highlight_.build(*strip_);
    if (!transport_ok)
        return FlowBuildResult::TransportFailed;

    // Expected notes + tie continuations for the gate (same id space —
    // no model bridge needed, plans/scoreview-gate.md).
    flow_.prepare_gates(
        [this](const std::string& id) { return engine_->midi_pitch_for_element(id); },
        engine_->tie_end_ids());
    return FlowBuildResult::Ok;
}

bool ScoreHost::rebuild_window(int first_bar, double stream_position_q, bool carry)
{
    // `first_bar` indexes STREAM SLOTS when the composer drives the program
    // (S3); with the composer inactive it is a plain source-bar index.
    const auto swap_start = std::chrono::steady_clock::now();
    const int window_span = 1 + kWindowHistoryBars + kWindowAheadBars;
    std::string window;
    int count = 0;
    if (composing_)
    {
        const int available = composer_.ensure(first_bar + window_span);
        first_bar = std::clamp(first_bar, 0, std::max(0, available - 1));
        count = std::min(window_span, available - first_bar);
        std::vector<SourceSlicer::StreamBar> items;
        items.reserve(static_cast<size_t>(count));
        for (int slot = first_bar; slot < first_bar + count; ++slot)
        {
            const StreamBarPlan& plan = composer_.plan(slot);
            if (plan.kind != StreamBarPlan::Kind::Piece && slot > last_logged_plan_slot_)
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
        window = slicer_.window_xml_for(items, composer_.plan(first_bar).source_bar);
    }
    else
    {
        const int total = slicer_.bar_count();
        first_bar = std::clamp(first_bar, 0, std::max(0, total - 1));
        count = std::min(window_span, total - first_bar);
        window = slicer_.window_xml(first_bar, count);
    }
    const FlowController::CarryState carried = flow_.carry_state();

    std::string error;
    if (window.empty() || !engine_->load(window, error))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: window build failed (%s), monolithic strip",
            error.c_str());
        stream_windowed_ = false;
        return false;
    }
    engine_holds_window_ = true;
    if (build_flow_from_engine(error) != FlowBuildResult::Ok)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: window flow failed (%s), monolithic strip",
            error.c_str());
        stream_windowed_ = false;
        flow_dirty_ = true; // reload the full piece on the next pump
        return false;
    }
    window_first_bar_ = first_bar;
    window_bar_count_ = count;
    stream_offset_q_ = composing_ ? composer_.slot_start_q(first_bar) : slicer_.bar_start_q(first_bar);
    flow_.set_marking_qpm(piece_marking_qpm_);
    flow_.set_mode(FlowController::TransportMode::Roll);
    if (carry)
    {
        flow_.restore_carry(carried);
        const double local_position = stream_position_q - stream_offset_q_;
        const double window_end_local = (composing_ ? composer_.slot_start_q(first_bar + count)
                                                    : slicer_.bar_start_q(first_bar + count))
            - stream_offset_q_;
        for (const auto& [key, verdict] : verdict_archive_)
        {
            const double local_q = key.first / 1000.0 - stream_offset_q_;
            if (local_q >= -1e-6 && local_q <= window_end_local)
                flow_.preset_verdict(local_q, key.second, verdict);
        }
        flow_.fast_forward_resolved(local_position);
        flow_.seek(local_position);
    }
    flow_.play();
    apply_verdict_update(); // repaint carried verdicts on the fresh strip
    const double swap_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - swap_start)
                               .count();
    DRAXUL_LOG_DEBUG(LogCategory::App, "score: window -> bars %d..%d (%.1f ms)", first_bar,
        first_bar + count - 1, swap_ms);
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
    return true;
}

void ScoreHost::maybe_advance_stream()
{
    if (!stream_active() || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    const double stream_q = stream_position_q();
    int playhead_bar = 0;
    if (composing_)
    {
        if (composer_.finished()
            && window_first_bar_ + window_bar_count_ >= composer_.planned())
            return; // the program is complete and the window reaches its end
        playhead_bar = composer_.slot_at(stream_q);
    }
    else
    {
        if (window_first_bar_ + window_bar_count_ >= slicer_.bar_count())
            return; // the window tail already reaches the final bar
        playhead_bar = slicer_.bar_at(stream_q);
    }
    if (playhead_bar <= window_first_bar_ + kWindowHistoryBars)
        return; // still inside the history margin
    rebuild_window(playhead_bar - kWindowHistoryBars, stream_q, /*carry=*/true);
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
    if (game_mode_ == FlowController::TransportMode::Roll && stream_windowed_ && slicer_.ready())
    {
        // The runner plays on the rolling window from stream bar 0, with
        // a fresh program (mastery persists in the player model, not here).
        verdict_archive_.clear();
        composer_.reset();
        last_logged_plan_slot_ = -1;
        if (!rebuild_window(0, 0.0, /*carry=*/false))
            flow_.set_mode(game_mode_); // fell back to the monolithic strip
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
    player_input_.reset();
    keyboard_input_ = nullptr;
    mic_input_ = nullptr;
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
            keyboard_input_->push(anchor + 1, now_seconds());
            return true;
        }
        const size_t fluffed = rng % expected.size();
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (i != fluffed)
                keyboard_input_->push(expected[i], now_seconds());
        }
        const int direction = (rng & 0x10000u) != 0 ? 1 : -1;
        int wrong = expected[fluffed] + direction;
        while (std::find(expected.begin(), expected.end(), wrong) != expected.end())
            wrong += direction;
        keyboard_input_->push(wrong, now_seconds());
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
    if (progress_path_.empty() || player_model_.session_active())
        return;
    player_model_.begin_session(now_iso8601());
    session_start_ = std::chrono::steady_clock::now();
    // Resume at yesterday's pace: the stored tempo informs the start, still
    // clamped to the marking band. Fresh pieces keep the 60% default.
    if (player_model_.last_tempo_frac() > 0.0 && flow_.ready())
        flow_.set_tempo_qpm(flow_.marking_qpm() * player_model_.last_tempo_frac());
}

void ScoreHost::end_progress_session()
{
    if (!player_model_.session_active())
        return;
    const int seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - session_start_)
            .count());
    player_model_.end_session(
        seconds, flow_.marking_qpm() > 0.0 ? flow_.tempo_qpm() / flow_.marking_qpm() : 0.0);
    save_progress(/*final_flush=*/true);
}

void ScoreHost::save_progress(bool final_flush)
{
    if (progress_path_.empty())
        return;
    std::string error;
    if (!save_progress_atomic(progress_path_, player_model_.serialize(), error))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: progress save failed: %s", error.c_str());
        return;
    }
    progress_dirty_ = false;
    if (final_flush)
    {
        DRAXUL_LOG_INFO(LogCategory::App, "score: progress saved — %d notes total, %s",
            player_model_.total_notes_judged(), progress_path_.string().c_str());
    }
}

bool ScoreHost::ensure_tick_stream()
{
    if (tick_stream_ != nullptr)
        return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: metronome audio init failed: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = metronome_.tuning().sample_rate;
    tick_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (tick_stream_ == nullptr)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: metronome output unavailable: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(tick_stream_); // device streams open paused
    DRAXUL_LOG_INFO(LogCategory::App, "score: metronome output open (%d Hz)",
        metronome_.tuning().sample_rate);
    return true;
}

void ScoreHost::cycle_tick_level()
{
    tick_level_ = tick_level_ == TickLevel::Off
        ? TickLevel::Beats
        : (tick_level_ == TickLevel::Beats ? TickLevel::Eighths : TickLevel::Off);
    if (tick_level_ != TickLevel::Off && !ensure_tick_stream())
        tick_level_ = TickLevel::Off;
    if (tick_level_ == TickLevel::Off && tick_stream_ != nullptr)
    {
        SDL_ClearAudioStream(tick_stream_);
        metronome_.clear();
    }
    DRAXUL_LOG_INFO(LogCategory::App, "score: metronome %s",
        tick_level_ == TickLevel::Off ? "off"
                                      : (tick_level_ == TickLevel::Beats ? "beats" : "eighths"));
}

void ScoreHost::pump_metronome(double p0_q, double p1_q, double dt)
{
    if (!ensure_tick_stream())
    {
        tick_level_ = TickLevel::Off;
        return;
    }
    const int rate = metronome_.tuning().sample_rate;

    // Schedule ticks for every grid crossing in (p0, p1], placed at their
    // fractional offset inside this pump's time slice so beat spacing
    // tracks the transport rather than the frame rate.
    if (p1_q > p0_q && dt > 0.0)
    {
        const double step = tick_level_ == TickLevel::Eighths ? 0.5 : 1.0;
        double grid = (std::floor(p0_q / step + 1e-9) + 1.0) * step;
        for (; grid <= p1_q + 1e-9; grid += step)
        {
            const double fraction = std::clamp((grid - p0_q) / (p1_q - p0_q), 0.0, 1.0);
            const int64_t at = metronome_.cursor() + static_cast<int64_t>(fraction * dt * static_cast<double>(rate));
            TickKind kind = TickKind::Subdivision;
            if (std::abs(grid - std::round(grid)) < 1e-6)
            {
                const double in_bar = std::fmod(std::round(grid), quarters_per_bar_);
                kind = std::abs(in_bar) < 1e-6 ? TickKind::Accent : TickKind::Beat;
            }
            metronome_.schedule_tick(at, kind);
        }
    }

    // Keep ~70 ms queued so the device never starves between pumps.
    constexpr int kTargetSamples = 3072;
    const int queued_bytes = SDL_GetAudioStreamQueued(tick_stream_);
    const int queued = queued_bytes > 0 ? queued_bytes / static_cast<int>(sizeof(float)) : 0;
    if (queued < kTargetSamples)
    {
        const size_t need = static_cast<size_t>(kTargetSamples - queued);
        tick_buffer_.resize(need);
        metronome_.render(tick_buffer_.data(), need);
        SDL_PutAudioStreamData(
            tick_stream_, tick_buffer_.data(), static_cast<int>(need * sizeof(float)));
    }
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
        const int slot = composer_.slot_at(stream_position_q());
        if (slot < composer_.planned())
            return composer_.plan(slot).source_bar + 1;
    }
    return static_cast<int>(stream_position_q() / quarters_per_measure) + 1;
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
        const double position_before_q = flow_.position_q();
        flow_.advance(dt);
        if (tick_level_ != TickLevel::Off)
            pump_metronome(position_before_q, flow_.position_q(), dt);
        if (flow_.mode() != FlowController::TransportMode::Clock)
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

            // Player memory: verdicts archive on the STREAM axis (for
            // window repaints), then translate through the program's
            // provenance — a review of bar 3 trains bar 3's statistics,
            // and drill outcomes train pitch/chord stats only.
            for (FlowController::NoteOutcome outcome : flow_.take_note_outcomes())
            {
                const double stream_q = outcome.onset_q + stream_offset_q_;
                if (!outcome.stray)
                {
                    verdict_archive_[{ static_cast<long long>(
                                           std::llround(stream_q * 1000.0)),
                        outcome.pitch }] = outcome.verdict;
                }
                if (composing_ && !outcome.stray)
                {
                    const int slot = composer_.slot_at(stream_q);
                    const StreamBarPlan& plan = composer_.plan(slot);
                    outcome.onset_q = plan.kind == StreamBarPlan::Kind::Drill
                        ? PlayerModel::kDrillOnsetSentinel
                        : slicer_.bar_start_q(plan.source_bar)
                            + (stream_q - composer_.slot_start_q(slot));
                }
                else
                {
                    outcome.onset_q = stream_q;
                }
                player_model_.apply(outcome);
                progress_dirty_ = true;
            }
            for (FlowController::ChordOutcome outcome : flow_.take_chord_outcomes())
            {
                outcome.onset_q += stream_offset_q_;
                player_model_.apply(outcome);
                progress_dirty_ = true;
            }
            const int bar = static_cast<int>(stream_position_q() / quarters_per_bar_);
            if (bar != last_flush_bar_)
            {
                last_flush_bar_ = bar;
                if (progress_dirty_)
                    save_progress(/*final_flush=*/false);
            }
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
        FlowBand band = flow_band();
        if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
        {
            // The rolling window sizes by WIDTH: about kStreamVisibleBars
            // bars around the playhead fill the pane (zoom shows fewer or
            // more), clamped so the band never overflows vertically.
            const double visible_q = std::max(1.0, kStreamVisibleBars * quarters_per_bar_ / std::max(0.25f, zoom_));
            const double span_canvas = flow_.x_at(flow_.position_q() + visible_q) - flow_.x_at(flow_.position_q());
            if (span_canvas > 1.0)
            {
                const float width_scale = static_cast<float>(vw / span_canvas);
                band.target_h = std::clamp(strip->canvas_size.y * width_scale,
                    96.0f * pixel_scale, vh * 0.9f);
                band.strip_y = (vh - band.target_h) * 0.5f;
            }
        }
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
                // Restart the stream from bar 0 with a fresh session.
                verdict_archive_.clear();
                composer_.reset();
                last_logged_plan_slot_ = -1;
                rebuild_window(0, 0.0, /*carry=*/false);
            }
            else
            {
                flow_.rewind();
                apply_lit_update();
            }
            break;
        case SDLK_T:
            cycle_tick_level();
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
        if (flow_.mode() != FlowController::TransportMode::Clock)
        {
            const bool roll = flow_.mode() == FlowController::TransportMode::Roll;
            status += !roll && flow_.waiting()
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
            if (tick_level_ != TickLevel::Off)
                status += tick_level_ == TickLevel::Beats ? "  tick" : "  tick8";
            if (roll)
            {
                const int acc = static_cast<int>(std::lround(flow_.accuracy_ema() * 100.0));
                status += "  acc " + std::to_string(acc) + "%";
                if (composing_ && stream_active())
                {
                    const int slot = composer_.slot_at(stream_position_q());
                    if (slot < composer_.planned())
                    {
                        const StreamBarPlan::Kind kind = composer_.plan(slot).kind;
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
        if (tick_level_ != TickLevel::Off)
            status += tick_level_ == TickLevel::Beats ? "  tick" : "  tick8";
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

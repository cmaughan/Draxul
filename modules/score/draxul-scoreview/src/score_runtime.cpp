#include <draxul/scoreview/score_runtime.h>

#include <draxul/scoreview/stream_composer.h>

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
#include "score_presentation.h"
#include "score_session_controller.h"
#include "score_stream_controller.h"
#include "score_view_model.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

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

// Reading-room backdrop behind the pages; drawn inside the pane only. The
// host's default_background stays the app-standard terminal color so window
// clear, pane borders, and grid remainder strips match every other pane.

} // namespace

// Out of line so the unique_ptr members over internal component types
// (ScoreAudioController) destroy where the complete type is visible.
ScoreRuntime::ScoreRuntime()
    : audio_(std::make_unique<ScoreAudioController>())
    , session_(std::make_unique<ScoreSessionController>())
    , stream_(std::make_unique<ScoreStreamController>())
    , presentation_(std::make_unique<ScorePresentation>())
{
}

double ScoreRuntime::stream_position_q() const
{
    return flow_.position_q() + stream_->stream_offset_q();
}

bool ScoreRuntime::stream_active() const
{
    return stream_->active();
}

ScoreRuntime::~ScoreRuntime() = default;

bool ScoreRuntime::initialize(const HostContext& context,
    ScoreRuntimeCallbacks& callbacks, ScoreRuntimePaths paths)
{
    quiesced_ = false;
    viewport_ = context.initial_viewport;
    callbacks_ = &callbacks;
    source_path_ = context.launch_options.source_path;
    background_ = context.launch_options.terminal_bg.value_or(background_);
    device_leases_ = paths.device_leases
        ? std::move(paths.device_leases) : process_score_device_leases();

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

        if (paths.verovio_data.empty())
            paths.verovio_data = executable_directory() / "verovio-data";
        if (paths.soundfonts.empty())
            paths.soundfonts = executable_directory() / "soundfonts";
        if (paths.progress.empty())
        {
            paths.progress = ConfigDocument::default_path().parent_path()
                / "scoreview" / "progress";
        }
        const std::string resources = paths.verovio_data.string();
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
        stream_->adopt_engraver(WindowEngraver::create(resources, engraver_error));
        if (!stream_->engraver_available())
            DRAXUL_LOG_WARN(LogCategory::App,
                "score: background engraver unavailable (%s); window swaps stay synchronous",
                engraver_error.c_str());

        // Instrument voices: every .sf2 staged beside the app is offered in
        // the inspector (the bundled YDP grand by default; a better font
        // dropped into the folder just appears). Loading is lazy — the
        // ~118 MB parse happens on first selection, not startup.
        audio_->stage_soundfonts(paths.soundfonts);
        audio_->prefer_piano(0);

        // Player memory: the per-piece progress file, keyed by the source
        // bytes so renames don't lose history (stream plan S0).
        session_->attach_source(paths.progress, bytes);

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
            stream_->set_windowed(false);
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
        // The green analysis annotations over the paged reading view (the
        // inspector's "Analysis overlay"); launch token for headless
        // screenshots and quick inspection.
        if (command.find("analysis") != std::string::npos)
            show_analysis_overlay_ = true;
        if (command.find("unique") != std::string::npos)
            show_unique_chunks_ = true;
        // The composer is ON by default (C6); `nocomposer` opts out at
        // launch, `composer` is kept as an explicit no-op opt-in (checked
        // second — "composer" is a substring of "nocomposer").
        if (command.find("nocomposer") != std::string::npos)
            composer_enabled_ = false;
        else if (command.find("composer") != std::string::npos)
            composer_enabled_ = true;
        // `composer=<name>` picks the IComposer (kanban 22 selection seam); the
        // bare `composer` token above already enabled it. Unknown names fall
        // back to the default when select_composer runs at flow-build time.
        const std::string composer_tok = "composer=";
        if (const size_t eq = command.find(composer_tok); eq != std::string::npos)
        {
            const size_t start = eq + composer_tok.size();
            const size_t end = command.find_first_of(" \t", start);
            composer_name_ = command.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
        }
        // Fabricated drills/scales are opt-in while their value is evaluated.
        if (command.find("drills") != std::string::npos)
            composer_drills_ = true;
        if (command.find("scales") != std::string::npos)
            composer_scales_ = true;
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

double ScoreRuntime::now_seconds() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch_).count();
}

void ScoreRuntime::shutdown()
{
    quiesce();
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
}

void ScoreRuntime::quiesce()
{
    if (quiesced_)
        return;
    end_progress_session();
    // Stop the background engraver first: its destructor signals the worker and
    // joins it (bounded by one in-flight engrave), so nothing races teardown.
    stream_->reset_engraver();
    release_input_device();
    release_audio_output();
    audio_->shutdown();
    running_ = false;
    quiesced_ = true;
}

bool ScoreRuntime::is_running() const
{
    return running_;
}

void ScoreRuntime::set_viewport(const HostViewport& viewport)
{
    const bool size_changed = viewport.pixel_size != viewport_.pixel_size || viewport.pixel_scale != viewport_.pixel_scale;
    viewport_ = viewport;
    if (size_changed)
        layout_dirty_ = true;
}

void ScoreRuntime::set_presentation_visible(bool visible,
    bool allow_background_playback)
{
    background_playback_ = allow_background_playback;
    if (presentation_visible_ == visible)
        return;

    presentation_visible_ = visible;
    if (!visible && !background_playback_)
    {
        resume_transport_on_show_ = flow_.playing();
        if (resume_transport_on_show_)
            flow_.pause();
        release_input_device();
        release_audio_output();
    }
    else if (visible)
    {
        if (flow_.mode() != FlowController::TransportMode::Clock
            && gate_input_requested_ != GateInput::Keyboard)
        {
            set_gate_input(gate_input_requested_, 0.0,
                gate_bot_accuracy_, midi_port_requested_);
        }
        if (resume_transport_on_show_ && !flow_.at_end())
            flow_.play();
        resume_transport_on_show_ = false;
    }
    last_pump_ = std::chrono::steady_clock::now();
    if (callbacks_ != nullptr)
    {
        callbacks_->notify_presentation_changed();
        if (visible)
            callbacks_->request_frame();
    }
}

void ScoreRuntime::on_config_reloaded(const HostReloadConfig& config)
{
    if (config.terminal_bg)
        background_ = *config.terminal_bg;
}

float ScoreRuntime::ui_scale() const
{
    return viewport_.pixel_scale > 0.0f ? viewport_.pixel_scale : 1.0f;
}

ScoreRuntime::FlowBand ScoreRuntime::flow_band() const
{
    const float vh = static_cast<float>(viewport_.pixel_size.y);
    FlowBand band;
    band.target_h = std::clamp(vh * 0.35f * zoom_, 96.0f * ui_scale(), vh * 0.9f);
    band.strip_y = (vh - band.target_h) * 0.5f;
    band.band_pad = 18.0f * ui_scale();
    return band;
}

float ScoreRuntime::page_margin() const
{
    return 24.0f * ui_scale();
}

float ScoreRuntime::page_gap() const
{
    return 24.0f * ui_scale();
}

float ScoreRuntime::content_height() const
{
    if (!pages_ || pages_->empty())
        return 0.0f;
    const float count = static_cast<float>(pages_->size());
    return 2.0f * page_margin() + count * page_height_px_ + (count - 1.0f) * page_gap();
}

float ScoreRuntime::max_scroll() const
{
    return std::max(0.0f, content_height() - static_cast<float>(viewport_.pixel_size.y));
}

void ScoreRuntime::scroll_by(float delta_px)
{
    scroll_to(scroll_y_ + delta_px);
}

void ScoreRuntime::scroll_to(float scroll_px)
{
    const float clamped = std::clamp(scroll_px, 0.0f, max_scroll());
    if (clamped == scroll_y_)
        return;
    scroll_y_ = clamped;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreRuntime::set_zoom(float zoom)
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

int ScoreRuntime::current_page() const
{
    if (!pages_ || pages_->empty() || page_height_px_ <= 0.0f)
        return 1;
    const float focus = scroll_y_ + static_cast<float>(viewport_.pixel_size.y) * 0.5f - page_margin();
    const int page = static_cast<int>(std::floor(focus / (page_height_px_ + page_gap())));
    return std::clamp(page, 0, static_cast<int>(pages_->size()) - 1) + 1;
}

void ScoreRuntime::rebuild_analysis_overlay()
{
    // The green analysis annotations for the paged view. Built whenever the
    // pages are (cheap next to the engrave), so the inspector toggle costs
    // nothing — it only gates the draw.
    analysis_overlay_.reset();
    if (!pages_ || pages_->empty() || !engine_ || !engine_->is_loaded())
        return;
    std::string timemap_error;
    const auto timemap = parse_timemap(engine_->render_timemap(), timemap_error);
    if (!timemap || timemap->empty())
    {
        if (!timemap)
            DRAXUL_LOG_DEBUG(LogCategory::App, "score: analysis overlay timemap failed: %s",
                timemap_error.c_str());
        return;
    }
    // Bar starts on the analysis axis: the slicer's real bar geometry when
    // it has the piece, else uniform bars from the notated meter. The meter
    // comes from the model, not quarters_per_bar_ — that member is only set
    // once the conveyor builds, which paged-at-launch never does.
    std::vector<double> bar_starts;
    if (stream_->slicer().ready())
    {
        const int bars = stream_->slicer().bar_count();
        bar_starts.reserve(static_cast<size_t>(bars));
        for (int bar = 0; bar < bars; ++bar)
            bar_starts.push_back(stream_->slicer().bar_start_q(bar));
    }
    else
    {
        const double model_quarters = quarters_per_measure_from_model();
        const double qpb = model_quarters > 0.0 ? model_quarters : quarters_per_bar_;
        if (qpb > 0.0)
        {
            const int bars
                = std::max(1, static_cast<int>(std::ceil(timemap->duration_q / qpb)));
            bar_starts.reserve(static_cast<size_t>(bars));
            for (int bar = 0; bar < bars; ++bar)
                bar_starts.push_back(bar * qpb);
        }
    }
    if (bar_starts.empty())
        return;
    // Paged-at-launch never builds the conveyor, which is where the piece
    // analysis normally runs — produce it here from the same timemap so the
    // overlay (and the composer after it) has a profile either way. Routed
    // through set_piece_profile so the .analysis.json dump stays in step.
    const PieceProfile& existing = session_->piece_profile();
    if (existing.phrases.empty() && existing.chords.empty() && existing.figures.empty())
    {
        std::vector<AnalysisOnset> onsets = analysis_onsets_from_timemap(*timemap,
            [this](const std::string& id) { return engine_->midi_pitch_for_element(id); });
        if (!onsets.empty())
        {
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
            const double analysis_qpb
                = bar_starts.size() > 1 ? bar_starts[1] - bar_starts[0] : quarters_per_bar_;
            session_->set_piece_profile(
                analyze_piece(onsets, analysis_qpb, notated_fifths));
        }
    }
    analysis_overlay_ = std::make_shared<const AnalysisOverlay>(
        build_analysis_overlay(*pages_, *timemap, session_->piece_profile(), bar_starts,
            [this](const std::string& id) { return engine_->midi_pitch_for_element(id); }));
}

void ScoreRuntime::relayout()
{
    if (!engine_ || !engine_->is_loaded() || viewport_.pixel_size.x <= 0 || viewport_.pixel_size.y <= 0)
        return;

    // The paged (reading) view is always the WHOLE piece: if the engine is
    // currently holding a rolling-window slice (the Roll runner), reload the
    // full source first — otherwise pagination would show only the window.
    if (stream_->active())
    {
        std::string reload_error;
        if (engine_->load(source_bytes_, reload_error))
        {
            stream_->set_active(false);
            stream_->clear_window_geometry();
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
    auto page_colors = std::make_shared<std::vector<ScoreHighlightState>>();
    page_colors->reserve(pages_->size());
    for (const ScoreDrawList& page : *pages_)
    {
        ScoreHighlightState colors;
        colors.build(page);
        page_colors->push_back(std::move(colors));
    }
    std::string color_timemap_error;
    const auto color_timemap = parse_timemap(engine_->render_timemap(), color_timemap_error);
    if (color_timemap)
    {
        std::unordered_set<std::string> note_ids;
        for (const TimemapEntry& entry : color_timemap->entries)
            note_ids.insert(entry.note_on.begin(), entry.note_on.end());
        for (const std::string& id : note_ids)
        {
            const int midi = engine_->midi_pitch_for_element(id);
            if (midi < 0)
                continue;
            const int palette
                = guidance_palette_index(midi, engine_->note_letter_for_element(id));
            for (ScoreHighlightState& colors : *page_colors)
                colors.set_guidance(id, palette);
        }
    }
    else
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: full-score note colors unavailable: %s", color_timemap_error.c_str());
    }
    page_note_highlights_ = page_colors;
    rebuild_analysis_overlay();
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

void ScoreRuntime::relayout_flow()
{
    flow_dirty_ = false;
    if (!engine_ || !engine_->is_loaded())
        return;
    if (stream_->active())
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
        stream_->set_active(false);
        stream_->clear_window_geometry();
    }

    std::string error;
    const FlowBuildResult result = build_flow_from_engine(error);
    // A fresh engraving: re-fit the fixed score band's scale to the new
    // content (the window high-water-mark restarts from the first window).
    presentation_->invalidate_scale_lock();
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
        if (stream_->windowed() && !stream_->slicer().ready())
        {
            std::string slicer_error;
            if (!stream_->slicer().load(source_bytes_, slicer_error) || stream_->slicer().bar_count() < 2)
            {
                stream_->set_windowed(false);
                DRAXUL_LOG_WARN(LogCategory::App,
                    "score: rolling window unavailable, monolithic strip (%s)",
                    slicer_error.c_str());
            }
        }
        // Select the requested IComposer before configuring it (kanban 22
        // seam); an unknown name keeps the adaptive-stream default.
        if (!stream_->select_composer(composer_name_))
            DRAXUL_LOG_WARN(LogCategory::App, "score: unknown composer '%s', using '%s'",
                composer_name_.c_str(), stream_->composer().name());
        // Source compatibility is the composer's call (IComposer::supports):
        // the adaptive stream needs a single-part source for its grand-staff
        // drill bars; unsupported pieces stream the source verbatim.
        stream_->set_composing(composer_enabled_ && stream_->windowed()
            && stream_->composer().supports(stream_->slicer()));
        if (stream_->composing())
        {
            stream_->composer().set_drills_enabled(composer_drills_);
            stream_->composer().set_scales_enabled(composer_scales_);
            stream_->composer().configure(&stream_->slicer(), &session_->model(), &session_->piece_profile());
            reset_stream_plan();
        }

        // Piece analysis (stream plan S1): key, chords + nearings, motifs,
        // rhythm figures — from the judgment axis itself, dumped beside the
        // progress file for inspection and cached for the composer. Built
        // from the timemap so onsets carry DURATIONS (the sustain-aware
        // skyline needs them to keep accompaniment out of the melody).
        std::vector<AnalysisOnset> analysis_onsets;
        {
            std::string timemap_error;
            const auto analysis_timemap = parse_timemap(engine_->render_timemap(), timemap_error);
            if (analysis_timemap)
                analysis_onsets = analysis_onsets_from_timemap(*analysis_timemap,
                    [this](const std::string& id) { return engine_->midi_pitch_for_element(id); });
            else
                DRAXUL_LOG_DEBUG(LogCategory::App, "score: analysis timemap failed: %s",
                    timemap_error.c_str());
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
        // A fresh analysis invalidates any overlay built against the old
        // profile (the paged view may already be holding pages).
        rebuild_analysis_overlay();
        if (start_in_gate_)
        {
            start_in_gate_ = false;
            enter_gate_mode(
                gate_input_requested_, kBotPaceQpm, gate_bot_accuracy_, midi_port_requested_);
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

EngraveParams ScoreRuntime::current_engrave_params() const
{
    EngraveParams params;
    params.pixel_scale = ui_scale();
    params.marking_qpm = piece_marking_qpm_;
    params.lock_tempo = lock_tempo_;
    params.proportional_spacing = proportional_spacing_;
    params.spacing_linear = spacing_linear_override_;
    params.spacing_non_linear = spacing_non_linear_override_;
    return params;
}

bool ScoreRuntime::queue_stream_engrave(int first_bar, double stream_q, bool fallback_to_monolith)
{
    auto slice = stream_->build_window_slice(first_bar);
    if (!slice)
        return false;
    ScoreStreamController::PendingInstall intent;
    intent.stream_position_q = stream_q;
    intent.carry = true;
    intent.preserve_tempo = false;
    intent.fallback_to_monolith_on_error = fallback_to_monolith;
    return stream_->queue_engrave(std::move(*slice), current_engrave_params(), intent);
}

ScoreRuntime::FlowBuildResult ScoreRuntime::build_flow_from_engine(std::string& error)
{
    // The whole-piece flow build (Clock conveyor, the `mono` strip, and the
    // initial engraving) shares its extraction with a rolling window; only the
    // transport policy differs. This path leaves the mode/marking to the pump
    // and enter_gate_mode: engrave_loaded reads only the scale/spacing fields,
    // so the marking/lock current_engrave_params() also carries are ignored.
    EngravedWindow engraved;
    const EngraveResult result = engrave_loaded(*engine_, current_engrave_params(), engraved, error);
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
    note_staff_ = std::move(engraved.staves);
    waterfall_notes_ = std::move(engraved.waterfall);
    for (const auto& [id, palette] : note_palette_)
        highlight_.set_guidance(id, palette);
    return FlowBuildResult::Ok;
}

bool ScoreRuntime::rebuild_window(
    int first_bar, double stream_position_q, bool carry, bool preserve_tempo)
{
    const auto swap_start = std::chrono::steady_clock::now();
    auto slice = stream_->build_window_slice(first_bar);
    if (!slice)
    {
        stream_->cancel_async();
        DRAXUL_LOG_WARN(LogCategory::App, "score: window slice empty, monolithic strip");
        stream_->set_windowed(false);
        flow_dirty_ = true;
        return false;
    }

    const EngraveParams params = current_engrave_params();

    // Startup performs one synchronous build before interactive frames exist.
    // Every later rebuild uses the latest-wins worker and leaves the current
    // engraving visible until its replacement is ready.
    if (stream_->engraver_available() && stream_->initial_window_installed())
    {
        ScoreStreamController::PendingInstall intent;
        intent.stream_position_q = stream_position_q;
        intent.carry = carry;
        intent.preserve_tempo = preserve_tempo;
        intent.fallback_to_monolith_on_error = false;
        if (!stream_->queue_engrave(std::move(*slice), params, intent))
            return false;
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
        stream_->set_windowed(false);
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

void ScoreRuntime::install_window(EngravedWindow&& engraved, int first_bar, int count,
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
    note_staff_ = std::move(engraved.staves);
    waterfall_notes_ = std::move(engraved.waterfall);
    rebuild_highlight_from_palette();

    stream_->note_installed(first_bar, count, stream_offset_q);

    if (carry)
    {
        flow_.restore_carry(carried);
        const double local_position = stream_position_q - stream_->stream_offset_q();
        const double window_end_q = stream_->window_end_q(first_bar, count);
        stream_->verdict_archive().replay(stream_->stream_offset_q(), window_end_q,
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

void ScoreRuntime::rebuild_highlight_from_palette()
{
    if (!strip_)
        return;
    highlight_.build(*strip_);
    for (const auto& [id, palette] : note_palette_)
        highlight_.set_guidance(id, palette);
}

std::optional<ScoreRuntime::PlayheadSource> ScoreRuntime::playhead_source() const
{
    if (stream_->composing() && stream_active() && !stream_->program().empty())
    {
        const StreamProgram::SourceRef ref = stream_->program().source_at(stream_position_q());
        return PlayheadSource{ ref.source_bar, ref.drill };
    }
    return std::nullopt;
}

void ScoreRuntime::apply_tempo_ladder()
{
    // The per-bar tempo ladder (C3): entering a bar caps the roll tempo at
    // that bar's earned rung — clean passes raised it, fumbles lowered it.
    // The cap only pulls DOWN; the ramp back up stays the roll easing's job,
    // so tempo rises only through demonstrated accuracy. The tempo lock
    // always wins, and drills inherit whatever tempo the stream is at.
    if (lock_tempo_ || !stream_active() || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    int source_bar = -1;
    if (const auto source = playhead_source())
        source_bar = source->drill ? -1 : source->source_bar; // drills don't cap
    else if (!stream_->composing() && quarters_per_bar_ > 0.0)
        source_bar = static_cast<int>(stream_position_q() / quarters_per_bar_);
    if (source_bar < 0 || source_bar == ladder_bar_)
        return;
    ladder_bar_ = source_bar;
    // The cap is the MINIMUM rung across the PHRASE containing this bar, not
    // the bar alone: a passage is played no faster than its hardest bar
    // allows (the classic rule), so the pulse stays coherent across the
    // phrase instead of lurching bar-to-bar over the mastery terrain. As
    // every bar in the phrase improves, the phrase's floor rises — all
    // ships. Unplayed bars hold the rung at the start fraction, so the
    // stream never speeds into unread material. Without confident structure
    // the bar's own rung applies (no phrase to be coherent across).
    const PieceProfile& profile = session_->piece_profile();
    double cap_frac = session_->model().bar_tempo_ladder(source_bar);
    int cap_begin = source_bar;
    int cap_end = source_bar + 1;
    if (profile.structure_confidence >= StreamComposer::kMinStructureConfidence)
    {
        if (const PieceProfile::Phrase* phrase = profile.phrase_at_bar(source_bar))
        {
            cap_begin = phrase->start_bar;
            cap_end = phrase->end_bar;
            for (int bar = phrase->start_bar; bar < phrase->end_bar; ++bar)
                cap_frac = std::min(cap_frac, session_->model().bar_tempo_ladder(bar));
        }
    }
    const double cap_qpm = flow_.marking_qpm() * cap_frac;
    if (flow_.tempo_qpm() > cap_qpm)
    {
        flow_.set_tempo_qpm(cap_qpm);
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: tempo ladder caps bars %d..%d at %.0f qpm (weakest rung %.2f)",
            cap_begin + 1, cap_end, cap_qpm, cap_frac);
    }
}

void ScoreRuntime::restart_stream(bool keep_tempo)
{
    const double tempo = flow_.tempo_qpm();
    stream_->verdict_archive().clear();
    reset_stream_plan();
    rebuild_window(0, 0.0, /*carry=*/false, /*preserve_tempo=*/keep_tempo);
    if (keep_tempo && !lock_tempo_)
        flow_.set_tempo_qpm(tempo);
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreRuntime::reset_stream_plan()
{
    stream_->reset_plan();
}

void ScoreRuntime::reengrave_flow_in_place()
{
    presentation_->invalidate_scale_lock();
    if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
        rebuild_window(stream_->window_first_bar(), stream_position_q(), /*carry=*/true);
    else
        flow_dirty_ = true;
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreRuntime::maybe_urgent_rewrite()
{
    // The rewriting composer: a fumble's correction should not wait a full
    // engrave window. On the fumble EDGE (the model's monotonic dirty-pass
    // count advancing), splice the fix just past the playhead and re-engrave
    // the same window in the background — the sheet ahead of the player
    // updates while the current window keeps playing. Skipped while a swap
    // is already in flight or without the async worker (a synchronous
    // rewrite would freeze mid-play for no urgency win); the append path
    // (try_reserve) still catches the fumble at the next window then.
    const int dirty = session_->model().dirty_pass_count();
    if (dirty == seen_dirty_passes_)
        return;
    seen_dirty_passes_ = dirty;
    if (!stream_active() || flow_.mode() != FlowController::TransportMode::Roll
        || stream_->async_in_flight() || !stream_->engraver_available())
        return;
    const double stream_q = stream_position_q();
    if (!stream_->try_urgent_rewrite(stream_q))
        return;
    if (queue_stream_engrave(
            stream_->window_first_bar(), stream_q, /*fallback_to_monolith=*/true))
        DRAXUL_LOG_DEBUG(
            LogCategory::App, "score: urgent rewrite queued at stream q %.2f", stream_q);
}

void ScoreRuntime::maybe_advance_stream()
{
    if (!stream_active() || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    // A swap is already being engraved in the background; it installs in pump()
    // (poll_async_engrave). Don't queue a second one.
    if (stream_->async_in_flight())
        return;
    const double stream_q = stream_position_q();
    const std::optional<int> target = stream_->advance_target(stream_q, presentation_->stream_ahead_needed());
    if (!target)
        return;

    // Slice + plan on the main thread (cheap), then hand the heavy Verovio
    // engrave to the worker. The current window goes on playing until poll()
    // installs the result — no freeze, no wall-time catch-up.
    if (stream_->engraver_available())
    {
        queue_stream_engrave(*target, stream_q, /*fallback_to_monolith=*/true);
        return;
    }
    // No worker (creation failed): fall back to the synchronous swap, and don't
    // charge its wall-time to the transport (else the playhead jumps to catch
    // up across the freeze).
    const auto rebuild_start = std::chrono::steady_clock::now();
    rebuild_window(*target, stream_q, /*carry=*/true);
    last_pump_ += std::chrono::steady_clock::now() - rebuild_start;
}

void ScoreRuntime::poll_async_engrave()
{
    auto completed = stream_->poll_completed();
    if (!completed)
        return;
    apply_completed_engrave(std::move(completed->done), completed->intent.stream_position_q,
        completed->intent.carry, completed->intent.preserve_tempo,
        completed->intent.fallback_to_monolith_on_error);
}

void ScoreRuntime::handle_async_engrave_done(WindowEngraver::Done done)
{
    auto completed = stream_->filter_completion(std::move(done));
    if (!completed)
        return;
    apply_completed_engrave(std::move(completed->done), completed->intent.stream_position_q,
        completed->intent.carry, completed->intent.preserve_tempo,
        completed->intent.fallback_to_monolith_on_error);
}

void ScoreRuntime::apply_completed_engrave(WindowEngraver::Done done, double intent_position_q,
    bool carry, bool preserve_tempo, bool fallback_to_monolith)
{
    if (!done.ok)
    {
        // A failed speculative advance retains the old window until the
        // monolithic fallback can be rebuilt. Interactive restyling failures
        // simply keep the current valid engraving visible.
        if (fallback_to_monolith)
        {
            stream_->set_windowed(false);
            flow_dirty_ = true;
        }
        return;
    }
    // If the player left the rolling window while the engrave ran (paged view,
    // exited the game), drop the now-stale result rather than forcing Roll back.
    if (view_mode_ != ViewMode::Flow || flow_.mode() != FlowController::TransportMode::Roll)
        return;
    // Install at the CURRENT playhead (the old window kept advancing while the
    // engrave ran); carry replays the transport/verdicts onto the fresh strip.
    const int first_bar = done.first_bar;
    const int count = done.count;
    const auto install_start = std::chrono::steady_clock::now();
    const double install_position_q = carry ? stream_position_q() : intent_position_q;
    install_window(std::move(done.window), first_bar, count, done.stream_offset_q,
        install_position_q, carry, preserve_tempo);
    const double install_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - install_start)
                                  .count();
    DRAXUL_LOG_DEBUG(LogCategory::App, "score: window -> bars %d..%d (%.1f ms install, async)",
        first_bar, first_bar + count - 1, install_ms);
}

void ScoreRuntime::toggle_flow_mode()
{
    if (!engine_ || !engine_->is_loaded())
        return;
    stream_->cancel_async();
    if (view_mode_ == ViewMode::Paged)
    {
        view_mode_ = ViewMode::Flow;
        flow_dirty_ = true; // re-engrave as the strip on the next pump
        // Returning to the runner: if the piece supports the rolling Roll
        // window, re-arm it (the flow build re-enters it via start_in_gate_);
        // otherwise this falls through to the whole-piece conveyor.
        if (game_mode_ == FlowController::TransportMode::Roll && stream_->windowed()
            && stream_->slicer().ready())
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

void ScoreRuntime::apply_lit_update()
{
    const FlowController::LitUpdate update = flow_.take_lit_update();
    if (update.reset)
        highlight_.clear_lit();
    for (const std::string& id : update.newly_lit)
        highlight_.set_lit(id);
}

void ScoreRuntime::apply_verdict_update()
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

bool ScoreRuntime::set_gate_input(
    GateInput input, double bot_pace_qpm, double bot_accuracy, int midi_port)
{
    const GateInput requested = input;
    release_input_device();
    device_error_.clear();
    if (input == GateInput::Mic || input == GateInput::Midi)
    {
        if (!device_leases_)
            device_leases_ = process_score_device_leases();
        std::string device_name = "default";
        const ScoreDeviceKind kind = input == GateInput::Mic
            ? ScoreDeviceKind::Microphone : ScoreDeviceKind::MidiInput;
        if (input == GateInput::Midi)
        {
            const auto ports = PlayerInputRig::list_midi_ports();
            if (midi_port < 0 || midi_port >= static_cast<int>(ports.size()))
            {
                device_error_ = "selected MIDI input is no longer available";
                input = GateInput::Keyboard;
            }
            else
                device_name = ports[static_cast<size_t>(midi_port)];
        }
        if (input == GateInput::Mic || input == GateInput::Midi)
        {
            auto acquired = device_leases_->acquire(kind, device_name, this);
            if (!acquired.lease)
            {
                device_error_ = std::move(acquired.error);
                DRAXUL_LOG_WARN(LogCategory::App, "score: %s",
                    device_error_.c_str());
                input = GateInput::Keyboard;
            }
            else
                input_lease_ = std::move(acquired.lease);
        }
    }

    PlayerInputRig::Selection selection;
    selection.kind = input == GateInput::Bot ? PlayerInputRig::Kind::Bot
        : input == GateInput::Mic            ? PlayerInputRig::Kind::Mic
        : input == GateInput::Midi           ? PlayerInputRig::Kind::Midi
                                             : PlayerInputRig::Kind::Keyboard;
    selection.bot_pace_qpm = bot_pace_qpm;
    selection.bot_accuracy = bot_accuracy;
    selection.midi_port = midi_port;
    const bool engaged = input_rig_.select(selection, flow_);
    if (!engaged && (selection.kind == PlayerInputRig::Kind::Mic
            || selection.kind == PlayerInputRig::Kind::Midi))
        release_input_device();
    return engaged && input == requested;
}

bool ScoreRuntime::ensure_audio_output()
{
    if (!audio_->wants_pump())
    {
        release_audio_output();
        return true;
    }
    if (!audio_lease_)
    {
        if (!device_leases_)
            device_leases_ = process_score_device_leases();
        auto acquired = device_leases_->acquire(
            ScoreDeviceKind::AudioOutput, "default", this);
        if (!acquired.lease)
        {
            device_error_ = std::move(acquired.error);
            DRAXUL_LOG_WARN(LogCategory::App, "score: %s",
                device_error_.c_str());
            return false;
        }
        audio_lease_ = std::move(acquired.lease);
    }
    if (audio_->ensure_output_stream())
    {
        device_error_.clear();
        return true;
    }
    device_error_ = "default audio output is unavailable";
    release_audio_output();
    return false;
}

void ScoreRuntime::release_audio_output()
{
    audio_->shutdown();
    audio_lease_.reset();
}

void ScoreRuntime::release_input_device()
{
    input_rig_.clear();
    input_lease_.reset();
}

void ScoreRuntime::enter_gate_mode(
    GateInput input, double bot_pace_qpm, double bot_accuracy, int midi_port)
{
    if (!flow_.gates_ready())
        return;
    if (game_mode_ == FlowController::TransportMode::Roll && stream_->windowed() && stream_->slicer().ready())
    {
        // The runner plays on the rolling window from stream bar 0, with
        // a fresh program (mastery persists in the player model, not here).
        stream_->verdict_archive().clear();
        reset_stream_plan();
        if (!rebuild_window(0, 0.0, /*carry=*/false))
            flow_.set_mode(game_mode_); // fell back to the monolithic strip
        else if (stream_->async_in_flight())
            flow_.set_mode(game_mode_); // old strip stays interactive while replacing it
    }
    else
    {
        flow_.set_mode(game_mode_);
    }
    highlight_.clear_lit();
    apply_verdict_update(); // consume the reset
    set_gate_input(input, bot_pace_qpm, bot_accuracy, midi_port);
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

void ScoreRuntime::exit_gate_mode()
{
    end_progress_session();
    stream_->cancel_async();
    release_input_device();
    flow_.set_mode(FlowController::TransportMode::Clock);
    if (stream_active())
    {
        // The engine holds a window document; the clock conveyor wants the
        // whole piece back (relayout_flow reloads the source).
        stream_->verdict_archive().clear();
        flow_dirty_ = true;
    }
    highlight_.clear_lit();
    apply_lit_update(); // consume the reset; re-light anything at q <= 0
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

bool ScoreRuntime::handle_gate_key(int keycode)
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
        gate_input_requested_ = engaged ? next : GateInput::Keyboard;
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

double ScoreRuntime::quarters_per_measure_from_model() const
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

void ScoreRuntime::begin_progress_session()
{
    if (!session_->begin_session())
        return;
    // Resume at yesterday's pace: the stored tempo informs the start, still
    // clamped to the marking band. Fresh pieces keep the 60% default. The
    // tempo lock overrides this — it holds the marking regardless.
    if (!lock_tempo_ && session_->model().last_tempo_frac() > 0.0 && flow_.ready())
        flow_.set_tempo_qpm(flow_.marking_qpm() * session_->model().last_tempo_frac());
}

void ScoreRuntime::end_progress_session()
{
    session_->end_session(
        flow_.marking_qpm() > 0.0 ? flow_.tempo_qpm() / flow_.marking_qpm() : 0.0);
}

void ScoreRuntime::clear_piece_progress()
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

int ScoreRuntime::approx_measure() const
{
    if (!flow_.ready() || !has_model_)
        return 0;
    const double quarters_per_measure = quarters_per_measure_from_model();
    if (quarters_per_measure <= 0.0)
        return 0;
    if (const auto source = playhead_source())
        return source->source_bar + 1; // a drill shows its reference bar
    return static_cast<int>(stream_position_q() / quarters_per_measure) + 1;
}

void ScoreRuntime::pump()
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
    const bool background_engrave_active = stream_->engrave_busy();
    if (view_mode_ == ViewMode::Flow && flow_dirty_ && !background_engrave_active)
        relayout_flow();
    if (view_mode_ == ViewMode::Paged && layout_dirty_ && !background_engrave_active)
        relayout();

    if (view_mode_ == ViewMode::Flow && flow_.playing())
    {
        const double position_before_q = flow_.position_q();
        flow_.advance(dt);
        if (audio_->wants_pump())
        {
            if (ensure_audio_output())
            {
                audio_->pump(position_before_q, flow_.position_q(), dt,
                    quarters_per_bar_, flow_);
            }
            else
            {
                audio_->set_tick_level(ScoreAudioController::TickLevel::Off);
                audio_->set_audition(false);
            }
        }
        if (flow_.mode() != FlowController::TransportMode::Clock)
        {
            if (input_rig_.mic_failed())
            {
                DRAXUL_LOG_WARN(LogCategory::App, "score: %s — falling back to keyboard input",
                    input_rig_.mic_error().c_str());
                gate_input_requested_ = GateInput::Keyboard;
                set_gate_input(GateInput::Keyboard, 0.0, 1.0);
            }
            if (input_rig_.active())
            {
                std::vector<PlayerNoteEvent> events;
                input_rig_.input()->poll(now_seconds(), events);
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
                // Hand attribution from the engraving (C5): the staff was
                // captured at engrave time — the async engraver means the
                // live engine may hold a different document by now.
                if (!outcome.stray && !outcome.id.empty())
                {
                    const auto staff = note_staff_.find(outcome.id);
                    outcome.staff = staff != note_staff_.end() ? staff->second : 0;
                }
                const double stream_q = outcome.onset_q + stream_->stream_offset_q();
                if (!outcome.stray)
                    stream_->verdict_archive().record(stream_q, outcome.pitch, outcome.verdict);
                if (stream_->composing() && !outcome.stray)
                {
                    const StreamProgram::SourceRef ref = stream_->program().source_at(stream_q);
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
                outcome.onset_q += stream_->stream_offset_q();
                session_->model().apply(outcome);
                session_->mark_dirty();
            }
            session_->flush_at_bar(
                static_cast<int>(stream_position_q() / quarters_per_bar_));
            apply_tempo_ladder();
            maybe_urgent_rewrite();
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

std::optional<std::chrono::steady_clock::time_point> ScoreRuntime::next_deadline() const
{
    if (stream_->engrave_busy())
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    if (view_mode_ == ViewMode::Flow && flow_.playing())
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    return std::nullopt;
}

void ScoreRuntime::draw(ScoreFrameSink& frame)
{
    if (!nanovg_pass_)
        return;

    const float pixel_scale = ui_scale();
    const bool retain_flow_while_paged_layout_waits = view_mode_ == ViewMode::Paged
        && layout_dirty_ && (!pages_ || pages_->empty()) && stream_->engrave_busy();
    if ((view_mode_ == ViewMode::Flow || retain_flow_while_paged_layout_waits) && strip_
        && strip_->canvas_size.y > 0.0f)
    {
        // The conveyor frame: the host gathers plain inputs; the presentation
        // component owns the composition and the fixed-scale lock.
        ScorePresentation::FlowFrame flow_frame;
        flow_frame.pixel_scale = pixel_scale;
        flow_frame.vw = static_cast<float>(viewport_.pixel_size.x);
        flow_frame.vh = static_cast<float>(viewport_.pixel_size.y);
        flow_frame.zoom = zoom_;
        const FlowBand band = flow_band();
        flow_frame.band_target_h = band.target_h;
        flow_frame.band_strip_y = band.strip_y;
        flow_frame.streaming
            = stream_active() && flow_.mode() == FlowController::TransportMode::Roll;
        flow_frame.show_waterfall = show_waterfall_;
        flow_frame.split_accidentals = split_accidentals_;
        flow_frame.score_height_frac = score_height_frac_;
        flow_frame.waterfall_beats = waterfall_beats_;
        flow_frame.note_gate = note_gate_;
        flow_frame.window_bar_count = stream_->window_bar_count();
        flow_frame.stream_offset_q = stream_->stream_offset_q();
        flow_frame.composing = stream_->composing();
        flow_frame.strip = strip_;
        flow_frame.highlight = &highlight_;
        flow_frame.note_palette = &note_palette_;
        flow_frame.waterfall_notes = &waterfall_notes_;
        flow_frame.flow = &flow_;
        flow_frame.program = &stream_->program();
        flow_frame.model = &session_->model();
        presentation_->record_flow(*nanovg_pass_, flow_frame);
    }
    else if (!pages_ || pages_->empty())
    {
        presentation_->record_placeholder(*nanovg_pass_, pixel_scale);
    }
    else
    {
        presentation_->record_paged(*nanovg_pass_, pages_, pixel_scale, page_margin(),
            page_gap(), scroll_y_, page_width_px_, page_height_px_, page_scale_,
            analysis_overlay_, page_note_highlights_, show_analysis_overlay_,
            show_unique_chunks_, show_note_colors_, split_accidentals_);
    }

    frame.record_canvas(*nanovg_pass_, viewport_.pixel_pos.x,
        viewport_.pixel_pos.y, viewport_.pixel_size.x,
        viewport_.pixel_size.y);
    // The debug inspector draws last, over the score.
    const auto imgui_now = std::chrono::steady_clock::now();
    const float imgui_dt = std::chrono::duration<float>(imgui_now - last_imgui_time_).count();
    last_imgui_time_ = imgui_now;
    render_debug_ui(imgui_dt);
    if (imgui_context_ != nullptr && imgui_backend_ != nullptr)
        frame.render_overlay(ImGui::GetDrawData(), imgui_context_);
    frame.finish();
}

ScoreViewModel ScoreRuntime::build_view_model() const
{
    // The ONE read point for the inspector: scalars by value, aggregates by
    // const pointer (stable for the frame — every mutation is deferred
    // through ScoreInspectorIntents).
    ScoreViewModel view;
    view.playing = flow_.playing();
    view.waiting = flow_.waiting();
    view.transport_mode = static_cast<int>(flow_.mode());
    view.position_q = flow_.position_q();
    view.stream_position_q = stream_position_q();
    view.tempo_qpm = flow_.tempo_qpm();
    view.min_tempo_qpm = flow_.min_tempo_qpm();
    view.max_tempo_qpm = flow_.max_tempo_qpm();
    view.marking_qpm = flow_.marking_qpm();
    view.stream_roll_active
        = stream_active() && flow_.mode() == FlowController::TransportMode::Roll;
    view.engraving_busy = stream_->async_in_flight() || stream_->engrave_busy();
    view.composer_name = stream_->composer().name();
    view.accuracy_ema = flow_.accuracy_ema();
    view.score = flow_.score();
    view.streak = flow_.streak();
    view.miss_count = flow_.miss_count();
    view.wrong_count = flow_.wrong_count();
    view.input_kind = input_rig_.kind();
    view.midi_port_name = input_rig_.midi_port_name();
    view.lock_tempo = lock_tempo_;
    view.show_waterfall = show_waterfall_;
    view.mark_mistakes = mark_mistakes_;
    view.show_note_colors = show_note_colors_;
    view.split_accidentals = split_accidentals_;
    view.composer_enabled = composer_enabled_;
    view.composer_drills = composer_drills_;
    view.composer_scales = composer_scales_;
    view.proportional_spacing = proportional_spacing_;
    view.show_analysis_overlay = show_analysis_overlay_;
    view.show_unique_chunks = show_unique_chunks_;
    view.score_height_frac = score_height_frac_;
    view.waterfall_beats = waterfall_beats_;
    view.note_gate = note_gate_;
    view.spacing_linear_override = spacing_linear_override_;
    view.spacing_non_linear_override = spacing_non_linear_override_;
    view.tick_level = static_cast<int>(audio_->tick_level());
    view.audition = audio_->audition();
    view.piano_voice = audio_->voice() == ScoreAudioController::Voice::Piano;
    view.loaded_soundfont_index = audio_->loaded_soundfont_index();
    view.selected_soundfont_index = audio_->selected_soundfont_index();
    view.soundfonts = &audio_->soundfonts();
    view.model = &session_->model();
    view.profile = &session_->piece_profile();
    view.program = &stream_->program();
    view.composing = stream_->composing();
    view.bar_count = stream_->slicer().bar_count();
    return view;
}

void ScoreRuntime::apply_inspector_intents(const ScoreInspectorIntents& intents)
{
    // The ONE mutation point for the inspector, after the frame's NanoVG
    // callback is recorded and ImGui::Render has completed — mid-frame
    // mutation is structurally impossible.
    if (!intents.any())
        return;
    if (intents.toggle_play)
    {
        if (flow_.playing())
            flow_.pause();
        else
            flow_.play();
    }
    if (intents.rewind_or_restart)
    {
        if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
        {
            restart_stream(/*keep_tempo=*/true);
        }
        else
        {
            flow_.rewind();
            apply_lit_update();
        }
    }
    if (intents.tempo_qpm)
        flow_.set_tempo_qpm(*intents.tempo_qpm);
    if (intents.lock_tempo)
    {
        lock_tempo_ = *intents.lock_tempo;
        flow_.set_adapt_tempo(!lock_tempo_);
        if (lock_tempo_ && flow_.marking_qpm() > 0.0)
            flow_.set_tempo_qpm(flow_.marking_qpm());
    }
    if (intents.select_keyboard_input)
    {
        gate_input_requested_ = GateInput::Keyboard;
        set_gate_input(GateInput::Keyboard, 0.0, 1.0);
    }
    if (intents.select_mic_input)
    {
        gate_input_requested_ = GateInput::Mic;
        set_gate_input(GateInput::Mic, 0.0, 1.0);
    }
    if (intents.select_midi_port)
    {
        gate_input_requested_ = GateInput::Midi;
        midi_port_requested_ = *intents.select_midi_port;
        if (set_gate_input(GateInput::Midi, 0.0, 1.0, *intents.select_midi_port)
            && !flow_.playing())
            flow_.play(); // the piano is the interface
    }
    if (intents.score_height_frac)
        score_height_frac_ = std::clamp(*intents.score_height_frac, 0.2f, 0.6f);
    if (intents.show_analysis_overlay)
    {
        show_analysis_overlay_ = *intents.show_analysis_overlay;
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
    }
    if (intents.show_unique_chunks)
    {
        show_unique_chunks_ = *intents.show_unique_chunks;
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
    }
    if (intents.show_waterfall)
    {
        show_waterfall_ = *intents.show_waterfall;
        presentation_->invalidate_scale_lock(); // re-fit the band layout
    }
    if (intents.waterfall_beats)
        waterfall_beats_ = *intents.waterfall_beats;
    if (intents.note_gate)
        note_gate_ = *intents.note_gate;
    if (intents.mark_mistakes)
    {
        mark_mistakes_ = *intents.mark_mistakes;
        if (!mark_mistakes_)
            highlight_.clear_lit(); // drop the current crosses at once
    }
    if (intents.show_note_colors)
        show_note_colors_ = *intents.show_note_colors;
    if (intents.split_accidentals)
        split_accidentals_ = *intents.split_accidentals;
    if (intents.composer_enabled)
    {
        // Re-evaluate and rebuild the stream from the top: on = the adaptive
        // program, off = the piece scrolling unchanged. The restart keeps the
        // player's decided tempo — switching the program is not a reason to
        // change their pace.
        composer_enabled_ = *intents.composer_enabled;
        stream_->set_composing(composer_enabled_ && stream_->windowed()
            && stream_->composer().supports(stream_->slicer()));
        if (stream_->composing())
        {
            stream_->composer().set_drills_enabled(composer_drills_);
            stream_->composer().set_scales_enabled(composer_scales_);
            stream_->composer().configure(
                &stream_->slicer(), &session_->model(), &session_->piece_profile());
        }
        restart_stream(/*keep_tempo=*/true);
    }
    if (intents.composer_drills)
    {
        // Live flip: the composer honors it for every slot planned from now
        // on; already-planned slots stand (they play out within a window).
        composer_drills_ = *intents.composer_drills;
        stream_->composer().set_drills_enabled(composer_drills_);
    }
    if (intents.composer_scales)
    {
        composer_scales_ = *intents.composer_scales;
        stream_->composer().set_scales_enabled(composer_scales_);
    }
    if (intents.proportional_spacing)
    {
        // Switching the preset drops any debug overrides — the point of the
        // toggle is the two tuned presets.
        proportional_spacing_ = *intents.proportional_spacing;
        spacing_linear_override_ = -1.0f;
        spacing_non_linear_override_ = -1.0f;
        reengrave_flow_in_place();
    }
    if (intents.spacing_linear_override)
    {
        spacing_linear_override_ = *intents.spacing_linear_override;
        reengrave_flow_in_place();
    }
    if (intents.spacing_non_linear_override)
    {
        spacing_non_linear_override_ = *intents.spacing_non_linear_override;
        reengrave_flow_in_place();
    }
    if (intents.reset_spacing_overrides)
    {
        spacing_linear_override_ = -1.0f;
        spacing_non_linear_override_ = -1.0f;
        reengrave_flow_in_place();
    }
    if (intents.tick_level)
    {
        audio_->set_tick_level(static_cast<ScoreAudioController::TickLevel>(*intents.tick_level));
        if (!ensure_audio_output())
            audio_->set_tick_level(ScoreAudioController::TickLevel::Off);
    }
    if (intents.use_synth_voice)
        audio_->use_synth();
    if (intents.use_piano_index)
        audio_->use_piano(*intents.use_piano_index);
    if (intents.audition)
    {
        audio_->set_audition(*intents.audition);
        if (!ensure_audio_output())
            audio_->set_audition(false);
    }
    if (intents.clear_progress)
        clear_piece_progress();
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreRuntime::on_key(const KeyEvent& event)
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
            if (!ensure_audio_output())
                audio_->set_tick_level(ScoreAudioController::TickLevel::Off);
            break;
        case SDLK_P:
            audio_->toggle_audition();
            if (!ensure_audio_output())
                audio_->set_audition(false);
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
    case SDLK_A:
        // The green analysis annotations (reading view only).
        show_analysis_overlay_ = !show_analysis_overlay_;
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
        break;
    case SDLK_S:
        // Unique-chunks: ghost restated phrases — the piece's actual size.
        show_unique_chunks_ = !show_unique_chunks_;
        if (callbacks_ != nullptr)
            callbacks_->request_frame();
        break;
    default:
        break;
    }
}

void ScoreRuntime::on_mouse_wheel(const MouseWheelEvent& event)
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

void ScoreRuntime::on_mouse_button(const MouseButtonEvent& event)
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

void ScoreRuntime::on_mouse_move(const MouseMoveEvent& event)
{
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().AddMousePosEvent(
        static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));
    if (callbacks_ != nullptr)
        callbacks_->request_frame();
}

void ScoreRuntime::on_text_input(const TextInputEvent& event)
{
    if (imgui_context_ == nullptr || event.text.empty())
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().AddInputCharactersUTF8(event.text.c_str());
}

void ScoreRuntime::attach_imgui_host(IImGuiHost& host)
{
    imgui_backend_ = &host;
    if (imgui_context_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    host.initialize_imgui_backend();
    host.rebuild_imgui_font_texture();
}

void ScoreRuntime::set_imgui_font(const std::string& path, float size_pixels)
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

bool ScoreRuntime::dispatch_action(std::string_view action)
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

void ScoreRuntime::request_close()
{
    running_ = false;
}

std::string ScoreRuntime::status_text() const
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
    if (!device_error_.empty())
        status += "  device: " + device_error_;
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
                if (stream_->composing() && stream_active())
                {
                    const int slot = stream_->program().slot_at(stream_position_q());
                    if (slot < stream_->program().size())
                    {
                        const StreamBarPlan::Kind kind = stream_->program().plan(slot).kind;
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

Color ScoreRuntime::default_background() const
{
    // The primary host's background becomes the window clear color, which
    // shows through pane borders and grid remainder strips — it must match
    // the terminal scheme exactly or every pane's chrome shifts hue. The
    // score's own mid-gray backdrop is drawn inside the pane instead.
    return background_;
}

HostRuntimeState ScoreRuntime::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = running_ && (!engine_ || pages_ != nullptr);
    return state;
}

HostDebugState ScoreRuntime::debug_state() const
{
    HostDebugState state;
    state.name = "ScoreView";
    return state;
}

HostPrintHint ScoreRuntime::print_hint() const
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

} // namespace scoreview
} // namespace draxul

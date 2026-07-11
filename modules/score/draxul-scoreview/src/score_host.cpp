#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/notation/musicxml_importer.h>
#include <draxul/runtime_path.h>
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

        // Dev/test hook (conveyor C4 screenshot verification): launching with
        // `--command flow` starts in the conveyor view; `--command
        // flow-autoplay` also starts the transport immediately.
        const std::string& command = context.launch_options.command;
        if (command.find("flow") != std::string::npos)
        {
            view_mode_ = ViewMode::Flow;
            flow_dirty_ = true;
            flow_autoplay_ = command.find("autoplay") != std::string::npos;
        }
    }

    last_pump_ = std::chrono::steady_clock::now();
    running_ = true;
    return true;
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
        apply_lit_update();
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
        const float target_h = std::clamp(vh * 0.35f * zoom_, 96.0f * pixel_scale, vh * 0.9f);
        const float scale = target_h / strip->canvas_size.y;
        constexpr double kAnchorFrac = 0.3;
        const double scroll_canvas = flow_.scroll_x(static_cast<double>(vw) / scale, kAnchorFrac);
        const float origin_x = static_cast<float>(-scroll_canvas * scale);
        const float strip_y = (vh - target_h) * 0.5f;
        const float playhead_x = static_cast<float>((flow_.x_at(flow_.position_q()) - scroll_canvas) * scale);

        nanovg_pass_->set_draw_callback(
            [strip, highlight, pixel_scale, vw, target_h, scale, origin_x, strip_y,
                playhead_x](NVGcontext* vg, int w, int h) {
                fill_backdrop(vg, w, h);
                const float band_pad = 18.0f * pixel_scale;
                draw_page_sheet(vg, -48.0f * pixel_scale, strip_y - band_pad,
                    vw + 96.0f * pixel_scale, target_h + 2.0f * band_pad, pixel_scale);
                const ScoreTextFonts fonts = ensure_score_text_fonts(vg);
                render_draw_list(vg, *strip, { origin_x, strip_y }, scale, fonts, highlight);
                nvgBeginPath(vg);
                nvgRect(vg, playhead_x - 1.0f * pixel_scale, strip_y - band_pad,
                    2.0f * pixel_scale, target_h + 2.0f * band_pad);
                nvgFillColor(vg, nvgRGBA(217, 115, 20, 170));
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

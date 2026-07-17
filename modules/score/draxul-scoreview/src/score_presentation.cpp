#include "score_presentation.h"

#include "score_stream_controller.h" // kWindowHistoryBars/kWindowAheadBars

#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/score_render_nvg.h>

#include "nanovg.h"

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace scoreview
{

namespace
{

constexpr float SQRT2 = 1.41421356f;
const NVGcolor BACKDROP = { { { 0.22f, 0.23f, 0.24f, 1.0f } } };
const NVGcolor INK = { { { 0.12f, 0.11f, 0.10f, 1.0f } } };

constexpr float STAFF_HEIGHT_FRAC = 0.045f;
constexpr float STAFF_LINE_THICKNESS_SP = 0.13f;
constexpr float BARLINE_THIN_SP = 0.16f;
constexpr float BARLINE_THICK_SP = 0.5f;
constexpr float BARLINE_SEPARATION_SP = 0.4f;
constexpr int PLACEHOLDER_MEASURES = 4;

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

void ScorePresentation::record_flow(INanoVGPass& pass, const FlowFrame& frame)
{
    // The conveyor: the whole piece as one strip on a full-width band,
    // scrolled so the playhead anchor tracks the transport position.
    auto strip = frame.strip;
    const ScoreHighlightState* highlight = frame.highlight;
    const float pixel_scale = frame.pixel_scale;
    const float vw = frame.vw;
    const float vh = frame.vh;
    const bool streaming = frame.streaming;
    const FlowController& flow = *frame.flow;
    // Layout (top to bottom): a FIXED score band (score_height_frac of the
    // pane height), then the waterfall, then the stumpy keyboard. The sheet
    // scales (locked) to fill the score band and is clipped to it, so it
    // never resizes or jumps as the window scrolls under the playhead.
    const bool show_wf
        = streaming && frame.show_waterfall && !frame.waterfall_notes->empty();
    const float score_region_h
        = std::clamp(vh * frame.score_height_frac, 96.0f * pixel_scale, vh * 0.9f);
    const float keyboard_h = streaming ? std::min(vw / 52.0f * 3.4f, vh * 0.16f) : 0.0f;
    const float keyboard_y = vh - keyboard_h;
    const float waterfall_h = show_wf ? std::max(0.0f, keyboard_y - score_region_h) : 0.0f;
    const float waterfall_top = keyboard_y - waterfall_h;
    float band_target_h = frame.band_target_h;
    float band_strip_y = frame.band_strip_y;
    if (streaming)
    {
        // Fit the scale to the TALLEST engraving (the full-piece extent
        // seeds it; a taller fabricated bar grows it once). The reference
        // only grows, so the sheet fills the band without ever clipping
        // off the bottom or jittering. Re-fit on viewport/zoom/waterfall/
        // band-fraction change, or when the reference grows.
        const float want_ref = std::max(stream_scale_ref_, strip->canvas_size.y);
        const int wf_key = show_wf ? 1 : 0;
        if (stream_scale_ <= 0.0f || stream_scale_vw_ != static_cast<int>(vw)
            || stream_scale_zoom_ != frame.zoom || stream_scale_wf_ != wf_key
            || stream_scale_frac_ != frame.score_height_frac || stream_scale_ref_ != want_ref)
        {
            stream_scale_ref_ = want_ref;
            const float usable = score_region_h * 0.9f;
            stream_scale_ = (usable / std::max(1.0f, want_ref)) * std::max(0.25f, frame.zoom);
            stream_scale_vw_ = static_cast<int>(vw);
            stream_scale_zoom_ = frame.zoom;
            stream_scale_wf_ = wf_key;
            stream_scale_frac_ = frame.score_height_frac;
            // Anchor the playhead at ~30% across, but never past the
            // history the window supplies (history_bars / visible_bars) —
            // otherwise the scroll clamps left and the playhead snaps back
            // when the window advances.
            const double avg_bar_canvas
                = strip->canvas_size.x / std::max(1, frame.window_bar_count);
            const double visible_bars
                = (static_cast<double>(vw) / std::max(0.001f, stream_scale_))
                / std::max(1.0, avg_bar_canvas);
            stream_anchor_ = static_cast<float>(std::min(0.30,
                static_cast<double>(ScoreStreamController::kWindowHistoryBars)
                    / std::max(1.0, visible_bars)));
            // The look-ahead the viewport shows to the right of the
            // playhead (+1 buffer) — the window advances when it drops to
            // this, so the sheet never runs out of notes ahead.
            stream_ahead_needed_ = std::clamp(
                static_cast<int>(std::ceil((1.0 - stream_anchor_) * visible_bars)) + 1, 1,
                ScoreStreamController::kWindowAheadBars);
        }
        // Fixed visual band, independent of the current window's canvas
        // extent — so the sheet backdrop and playhead never move.
        band_target_h = score_region_h * 0.9f;
        band_strip_y = (score_region_h - band_target_h) * 0.5f;
    }
    const float target_h = band_target_h;
    const float scale
        = streaming ? stream_scale_ : (target_h / std::max(1.0f, strip->canvas_size.y));
    const double anchor_frac = streaming ? stream_anchor_ : 0.30;
    const double scroll_canvas = flow.scroll_x(static_cast<double>(vw) / scale, anchor_frac);
    const float origin_x = static_cast<float>(-scroll_canvas * scale);
    const float strip_y = band_strip_y;
    const float playhead_x
        = static_cast<float>((flow.x_at(flow.position_q()) - scroll_canvas) * scale);
    const bool waiting = flow.waiting();

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
        const double position = flow.position_q();
        const auto& onsets = flow.onsets();
        const auto& gates = flow.gates();
        for (size_t i = 0; i < onsets.size() && i < gates.size(); ++i)
        {
            const double q = onsets[i].qstamp;
            if (q > position + FlowController::kRollEarlyWindowQ)
                break;
            if (q < position - FlowController::kRollLateWindowQ)
                continue;
            const double stream_q = q + frame.stream_offset_q;
            int trailing = 0;
            bool drill = false;
            if (frame.composing)
            {
                const StreamProgram::SourceRef ref = frame.program->source_at(stream_q);
                drill = ref.drill;
                if (!ref.drill)
                    trailing = frame.model->onset_trailing_correct(ref.source_q);
            }
            else
            {
                trailing = frame.model->onset_trailing_correct(stream_q);
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
                    const auto found = frame.note_palette->find(note.id);
                    const int palette = found != frame.note_palette->end()
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
    // sounds and dims when it ends. A note holds for note_gate of its
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
        const double position = flow.position_q();
        const float ppb = waterfall_h / static_cast<float>(std::max(1.0, frame.waterfall_beats));
        const float white_w = vw / 52.0f;
        const float gap = 2.0f * pixel_scale; // guaranteed daylight
        blocks.reserve(frame.waterfall_notes->size());
        for (const WaterfallNote& n : *frame.waterfall_notes)
        {
            const float span = static_cast<float>(n.duration_q) * ppb;
            const float sounding = static_cast<float>(n.duration_q * frame.note_gate) * ppb;
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
            const float bw = keyboard_is_black(n.midi) ? white_w * 0.52f : white_w * 0.74f;
            blocks.push_back({ cx - bw * 0.5f, bw, y0, y1, n.palette });
            // Light the key only while the note is sounding (the gated
            // length), so it dims before the next onset in a run.
            if (position + 1e-6 >= n.onset_q
                && position < n.onset_q + n.duration_q * frame.note_gate)
                lit.push_back({ n.midi, 1.0f, n.palette });
        }
    }
    const float keyboard_alpha = streaming ? 1.0f : 0.0f;
    const bool split_acc = frame.split_accidentals;

    const float score_clip_h = streaming ? score_region_h : vh;

    pass.set_draw_callback(
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
            nvgFillColor(vg, waiting ? nvgRGBA(24, 140, 165, 200) : nvgRGBA(217, 115, 20, 170));
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
                    const unsigned char* c = kGuidancePalette[b.palette % kGuidancePaletteSize];
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, b.x, b.y0, b.w, b.y1 - b.y0, 3.0f * pixel_scale);
                    nvgFillColor(vg, nvgRGBA(c[0], c[1], c[2], 235));
                    nvgFill(vg);
                }
                nvgRestore(vg);
                // The hit line where blocks meet the keys.
                nvgBeginPath(vg);
                nvgRect(vg, 0.0f, keyboard_y - 1.5f * pixel_scale, static_cast<float>(vw),
                    1.5f * pixel_scale);
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 46));
                nvgFill(vg);
            }
            if (streaming)
                draw_piano_keyboard(
                    vg, 0.0f, keyboard_y, vw, keyboard_h, lit, keyboard_alpha, pixel_scale);
        });
}

void ScorePresentation::record_placeholder(INanoVGPass& pass, float pixel_scale)
{
    pass.set_draw_callback([pixel_scale](NVGcontext* vg, int w, int h) {
        draw_placeholder(vg, w, h, pixel_scale);
    });
}

void ScorePresentation::record_paged(INanoVGPass& pass,
    std::shared_ptr<const std::vector<ScoreDrawList>> pages, float pixel_scale, float margin,
    float gap, float scroll, float page_w, float page_h, float scale,
    std::shared_ptr<const AnalysisOverlay> overlay, bool annotations, bool unique_chunks)
{
    if (!annotations && !unique_chunks)
        overlay = nullptr;
    pass.set_draw_callback(
        [pages, pixel_scale, margin, gap, scroll, page_w, page_h, scale, overlay, annotations,
            unique_chunks](NVGcontext* vg, int w, int h) {
            fill_backdrop(vg, w, h);
            const ScoreTextFonts fonts = ensure_score_text_fonts(vg);
            float y = margin - scroll;
            for (size_t index = 0; index < pages->size(); ++index)
            {
                const ScoreDrawList& page = (*pages)[index];
                if (y + page_h >= 0.0f && y <= static_cast<float>(h))
                {
                    draw_page_sheet(vg, margin, y, page_w, page_h, pixel_scale);
                    // Motif note coloring rides the guidance channel, exactly
                    // like the flow sheet's spelling colors — only when the
                    // analysis annotations are on.
                    const ScoreHighlightState* motif_colors
                        = overlay && annotations && index < overlay->highlights.size()
                        ? &overlay->highlights[index]
                        : nullptr;
                    render_draw_list(vg, page, { margin, y }, scale, fonts, motif_colors);
                    if (overlay && index < overlay->pages.size())
                    {
                        // Wash first: the green annotations stay legible on top.
                        if (unique_chunks)
                            draw_unique_wash(vg, overlay->pages[index], { margin, y }, scale,
                                pixel_scale, fonts);
                        if (annotations)
                            draw_analysis_overlay(vg, *overlay, overlay->pages[index],
                                { margin, y }, scale, pixel_scale, fonts, unique_chunks);
                    }
                }
                y += page_h + gap;
            }
            if (overlay && annotations)
                draw_analysis_banner(vg, *overlay, pixel_scale, fonts);
        });
}

} // namespace scoreview
} // namespace draxul

#pragma once

// The frame composer (kanban 21, ScorePresentation): NanoVG recording for
// the flow strip (score band + waterfall + guidance keyboard), the paged
// reading view, and the placeholder page — plus the presentation-owned
// scale-lock cache (the fixed sheet scale that keeps bars from breathing as
// the window advances). No device, persistence, or transport ownership: the
// host builds a plain input each frame and this component records it.
// Internal to draxul-scoreview-runtime (no public header).

#include <draxul/nanovg_pass.h>
#include <draxul/scoreview/analysis_overlay.h>
#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/stream_program.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{
namespace scoreview
{

class ScorePresentation
{
public:
    // The flow strip's frame input: plain values plus const views of the
    // frame's material. Everything here is read-only for the recording.
    struct FlowFrame
    {
        float pixel_scale = 1.0f;
        float vw = 0.0f;
        float vh = 0.0f;
        float zoom = 1.0f;
        // The reading-view band (host geometry); streaming overrides it with
        // the fixed score band below.
        float band_target_h = 0.0f;
        float band_strip_y = 0.0f;
        bool streaming = false; // rolling window + Roll mode
        bool show_waterfall = true;
        bool split_accidentals = true;
        float score_height_frac = 0.4f;
        double waterfall_beats = 7.0;
        double note_gate = 0.95;
        int window_bar_count = 1;
        double stream_offset_q = 0.0;
        bool composing = false;

        std::shared_ptr<const ScoreDrawList> strip;
        const ScoreHighlightState* highlight = nullptr;
        const std::unordered_map<std::string, int>* note_palette = nullptr;
        const std::vector<WaterfallNote>* waterfall_notes = nullptr;
        const FlowController* flow = nullptr;
        const StreamProgram* program = nullptr;
        const PlayerModel* model = nullptr;
    };

    void record_flow(INanoVGPass& pass, const FlowFrame& frame);
    void set_music_font_path(std::string path)
    {
        music_font_path_ = std::move(path);
    }
    void record_placeholder(INanoVGPass& pass, float pixel_scale);
    // `overlay` (optional): the analysis data — pages parallel to `pages`.
    // `annotations` draws the green analysis marks + banner; `unique_chunks`
    // ghosts restated phrases under a paper wash ('s', the piece's actual
    // size). Either flag alone works; the overlay is ignored when both off.
    void record_paged(INanoVGPass& pass,
        std::shared_ptr<const std::vector<ScoreDrawList>> pages, float pixel_scale,
        float margin, float gap, float scroll, float page_w, float page_h, float scale,
        std::shared_ptr<const AnalysisOverlay> overlay = nullptr,
        std::shared_ptr<const std::vector<ScoreHighlightState>> note_colors = nullptr,
        bool annotations = true, bool unique_chunks = false, bool show_note_colors = true,
        bool split_accidentals = true);

    // The look-ahead the viewport shows right of the playhead (+1 buffer) —
    // the stream advances when its runway drops to this. Computed by the
    // scale-lock fit; the host feeds it to the advance policy.
    int stream_ahead_needed() const
    {
        return stream_ahead_needed_;
    }
    // Drops the fixed-scale lock so the next flow frame re-fits (viewport,
    // spacing, or band-layout changes).
    void invalidate_scale_lock()
    {
        stream_scale_ = 0.0f;
        stream_scale_ref_ = 0.0f;
    }

private:
    std::string music_font_path_;
    // The guidance keyboard + fixed sheet scale (stream follow-up): the
    // scale locks per viewport/zoom so bars don't breathe as the window
    // moves. The reference canvas height only grows (the tallest window
    // engraving seen) so the sheet fills the band, never clips, and never
    // jitters.
    float stream_scale_ = 0.0f;
    int stream_scale_vw_ = 0;
    float stream_scale_zoom_ = 0.0f;
    int stream_scale_wf_ = -1; // waterfall-present part of the scale cache key
    float stream_scale_frac_ = 0.0f; // score-height part of the scale cache key
    float stream_scale_ref_ = 0.0f;
    // The scroll anchor (playhead's fraction across the viewport), capped at
    // the history the window actually provides so the scroll never clamps
    // and snaps the playhead on a window advance.
    float stream_anchor_ = 0.30f;
    int stream_ahead_needed_ = 14; // ScoreStreamController::kWindowAheadBars
};

} // namespace scoreview
} // namespace draxul

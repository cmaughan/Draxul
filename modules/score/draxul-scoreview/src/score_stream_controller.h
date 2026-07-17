#pragma once

// The rolling-window stream (kanban 21, ScoreStreamController): source
// slicing, the composer + host-owned program, the verdict archive, window
// bookkeeping (first bar / count / stream offset), the advance policy, and
// the async-engrave worker state machine (latest-wins generations over
// WindowEngraver). ScoreHost keeps what is genuinely presentation- and
// transport-coupled: the synchronous engrave through the main engine, the
// install swap, and the view/transport checks around a completed generation.
// Internal to draxul-scoreview-host (no public header).

#include <draxul/scoreview/composer.h>
#include <draxul/scoreview/source_slicer.h>
#include <draxul/scoreview/stream_composer.h>
#include <draxul/scoreview/stream_program.h>
#include <draxul/scoreview/verdict_archive.h>
#include <draxul/scoreview/window_engraver.h>

#include <memory>
#include <optional>
#include <string>

namespace draxul
{
namespace scoreview
{

class ScoreStreamController
{
public:
    // History bars kept behind the playhead. This must be enough to fill the
    // scroll anchor (~30% of the viewport) so the playhead can sit at the
    // anchor with music scrolling under it — too little and the scroll
    // clamps to the strip's left edge, snapping the playhead back on every
    // window advance.
    static constexpr int kWindowHistoryBars = 4;
    // Look-ahead bars in the window. Generous, because the window advances
    // only when the playhead nears its tail (not every bar) — the extra
    // ahead bars are the drift room between those (now rare) rebuilds.
    static constexpr int kWindowAheadBars = 14;

    ScoreStreamController();

    // A window's MusicXML slice plus where it sits in the stream. Produced
    // on the main thread (slicing + composer planning are cheap); the heavy
    // Verovio engrave then runs sync (host engine) or on the worker.
    struct WindowSlice
    {
        std::string xml;
        int first_bar = 0;
        int count = 0;
        double stream_offset_q = 0.0;
    };

    // What to do with a generation once the worker finishes it.
    struct PendingInstall
    {
        WindowEngraver::RequestId request_id = 0;
        double stream_position_q = 0.0;
        bool carry = false;
        bool preserve_tempo = false;
        bool fallback_to_monolith_on_error = false;
    };

    struct Completed
    {
        WindowEngraver::Done done;
        PendingInstall intent;
    };

    // --- source and mode -------------------------------------------------
    bool load_source(const std::string& bytes, std::string& error)
    {
        return slicer_.load(bytes, error);
    }
    SourceSlicer& slicer()
    {
        return slicer_;
    }
    const SourceSlicer& slicer() const
    {
        return slicer_;
    }
    // The rolling window vs the monolithic strip (`mono`, failure fallback).
    bool windowed() const
    {
        return windowed_;
    }
    void set_windowed(bool windowed)
    {
        windowed_ = windowed;
    }
    // Whether the main engine currently holds a window document.
    bool active() const
    {
        return active_;
    }
    void set_active(bool active)
    {
        active_ = active;
    }
    // Startup performs one synchronous build before interactive frames
    // exist; every later rebuild goes through the latest-wins worker.
    bool initial_window_installed() const
    {
        return initial_window_installed_;
    }
    void set_initial_window_installed(bool installed)
    {
        initial_window_installed_ = installed;
    }

    // --- composer / program / archive ------------------------------------
    IComposer& composer()
    {
        return *composer_;
    }
    StreamProgram& program()
    {
        return program_;
    }
    const StreamProgram& program() const
    {
        return program_;
    }
    VerdictArchive& verdict_archive()
    {
        return verdict_archive_;
    }
    bool composing() const
    {
        return composing_;
    }
    void set_composing(bool composing)
    {
        composing_ = composing;
    }
    // Drops the planned program AND the composer's slot-indexed policy state
    // together (they must never diverge), plus the plan-log cursor.
    void reset_plan();

    // --- window geometry --------------------------------------------------
    int window_first_bar() const
    {
        return window_first_bar_;
    }
    int window_bar_count() const
    {
        return window_bar_count_;
    }
    double stream_offset_q() const
    {
        return stream_offset_q_;
    }
    // The install swap succeeded: adopt the new window's geometry.
    void note_installed(int first_bar, int count, double stream_offset_q)
    {
        window_first_bar_ = first_bar;
        window_bar_count_ = count;
        stream_offset_q_ = stream_offset_q;
        active_ = true;
        initial_window_installed_ = true;
    }
    // Leaving the window for the monolithic strip: the stream axis becomes
    // the local axis again.
    void clear_window_geometry()
    {
        window_first_bar_ = 0;
        window_bar_count_ = 0;
        stream_offset_q_ = 0.0;
    }
    // Stream-axis end of a window (slot axis when composing, source bars
    // otherwise) — the verdict archive's replay bound.
    double window_end_q(int first_bar, int count) const;

    // --- planning ----------------------------------------------------------
    // `first_bar` indexes STREAM SLOTS when the composer drives the program
    // (S3); with the composer inactive it is a plain source-bar index.
    std::optional<WindowSlice> build_window_slice(int first_bar);
    // The advance policy: the next window's first bar when the playhead has
    // come within `ahead_needed` of the window's tail; nullopt otherwise
    // (including when the program/source is exhausted).
    std::optional<int> advance_target(double stream_q, int ahead_needed) const;

    // --- the async-engrave worker state machine ---------------------------
    void adopt_engraver(std::unique_ptr<WindowEngraver> engraver)
    {
        engraver_ = std::move(engraver);
    }
    bool engraver_available() const
    {
        return engraver_ != nullptr;
    }
    bool engrave_busy() const
    {
        return engraver_ && engraver_->busy();
    }
    bool async_in_flight() const
    {
        return async_in_flight_;
    }
    WindowEngraver::RequestId pending_request() const
    {
        return pending_install_ ? pending_install_->request_id : 0;
    }
    // Submits the slice as the newest generation (latest wins). False when
    // the worker rejects the job.
    bool queue_engrave(
        WindowSlice&& slice, const EngraveParams& params, PendingInstall intent);
    // Cancels the worker and drops any pending install.
    void cancel_async();
    // Destroys the worker (its destructor joins the thread) and drops any
    // pending install — the shutdown path.
    void reset_engraver()
    {
        engraver_.reset();
        pending_install_.reset();
        async_in_flight_ = false;
    }
    // Polls the worker; a completion is generation-checked here — stale
    // generations are dropped (logged) and never reach the host.
    std::optional<Completed> poll_completed();
    // The generation filter alone, for completions delivered out-of-band
    // (the deterministic test seam).
    std::optional<Completed> filter_completion(WindowEngraver::Done done);

private:
    SourceSlicer slicer_;
    bool windowed_ = true;
    bool active_ = false;
    bool initial_window_installed_ = false;

    std::unique_ptr<IComposer> composer_;
    StreamProgram program_;
    VerdictArchive verdict_archive_;
    bool composing_ = false;
    int last_logged_plan_slot_ = -1;

    int window_first_bar_ = 0;
    int window_bar_count_ = 0;
    double stream_offset_q_ = 0.0;

    std::unique_ptr<WindowEngraver> engraver_;
    bool async_in_flight_ = false;
    std::optional<PendingInstall> pending_install_;
};

} // namespace scoreview
} // namespace draxul

#pragma once

#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/layout_engine.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace draxul
{
namespace scoreview
{

// Engraves a rolling window on a background thread so the ~100ms Verovio
// serialize/parse round-trip no longer freezes the main (render/input) thread.
// It owns its OWN layout engine (a second Verovio toolkit) — the main thread's
// engine is never touched off-thread, and the two never lay out concurrently
// (the host defers synchronous engine work until this worker is idle).
//
// One engrave is in flight at a time, with at most one coalesced pending job.
// submit() is latest-wins and never waits: an older job may finish, but its
// result is discarded. cancel() is also non-blocking generation invalidation.
class WindowEngraver
{
public:
    using RequestId = uint64_t;

    struct Job
    {
        std::string window_xml;
        EngraveParams params;
        // Echoed back verbatim so the install knows where the window sits.
        int first_bar = 0;
        int count = 0;
        double stream_offset_q = 0.0;
    };

    struct Done
    {
        RequestId request_id = 0;
        EngravedWindow window;
        int first_bar = 0;
        int count = 0;
        double stream_offset_q = 0.0;
        bool ok = false;
    };

    // Returns nullptr and fills `error` when the background toolkit's resources
    // cannot be loaded (the host then falls back to synchronous rebuilds).
    static std::unique_ptr<WindowEngraver> create(const std::string& resource_dir, std::string& error);
    // Dependency-injected creation for deterministic worker/lifetime tests.
    static std::unique_ptr<WindowEngraver> create(
        std::unique_ptr<ILayoutEngine> engine, std::string& error);
    ~WindowEngraver();

    WindowEngraver(const WindowEngraver&) = delete;
    WindowEngraver& operator=(const WindowEngraver&) = delete;

    // True while a job is queued/running or a finished result awaits poll().
    bool busy() const;
    // Posts the newest desired job. While work is active, replaces the single
    // pending job. Returns zero only after shutdown has stopped submissions.
    RequestId submit(Job job);
    // Takes the finished result if one is ready; nullopt otherwise. On success
    // the worker returns to idle, ready for the next submit().
    std::optional<Done> poll();
    // Invalidates active/pending/ready generations without waiting. An active
    // engine call is allowed to finish, but its result cannot be polled.
    void cancel();

private:
    explicit WindowEngraver(std::unique_ptr<ILayoutEngine> engine);
    void worker_loop();

    std::unique_ptr<ILayoutEngine> engine_;

    enum class State : uint8_t
    {
        Idle, // no job, no pending result
        Working, // the worker is engraving
        Ready, // a result awaits poll()
    };

    mutable std::mutex mutex_;
    std::condition_variable job_cv_; // main -> worker: a job (or stop) posted
    State state_ = State::Idle;
    bool stop_ = false;
    struct Queued
    {
        RequestId request_id = 0;
        Job job;
    };
    Queued job_;
    std::optional<Queued> pending_job_;
    Done result_;
    RequestId next_request_id_ = 1;
    RequestId latest_request_id_ = 0;
    std::thread thread_;
};

} // namespace scoreview
} // namespace draxul
